#include <stdio.h>
#include <filesystem>
#include <vector>
#include "git2.h"
#include "efsw/efsw.hpp"
#include "simple-websocket-server/client_ws.hpp"
#include "boost/json.hpp"

using WsClient = SimpleWeb::SocketClient<SimpleWeb::WS>;

std::string map_action_to_string(efsw::Action action) {
  switch (action) {
    case efsw::Actions::Add:
      return "ADD";
    case efsw::Actions::Delete:
      return "DELETE";
    case efsw::Actions::Modified:
      return "MODIFIED";
    case efsw::Actions::Moved:
      return "MOVE";
    default:
      return "UNRECOGNIZED";
  }
}

bool is_hexadecimal(const std::string& str) {
  for (char c : str) {
    if (!std::isxdigit(c)) {
      return false;
    }
  }
  return true;
}

namespace graph {
  struct node {
    std::string id;
    std::string type;
    size_t size;
    std::string content;
  };
  struct edge {
    std::string id;
    std::string source;
    std::string target;
  };
  void tag_invoke(boost::json::value_from_tag, boost::json::value& jv, node const& n) {
    jv = {
      {"id" , n.id},
      {"type", n.type},
      {"size", n.size},
      {"content", n.content}
    };
  }
  void tag_invoke(boost::json::value_from_tag, boost::json::value& jv, edge const& e) {
    jv = {
      {"id" , e.id},
      {"source", e.source},
      {"target", e.target},
    };
  }
}

graph::node get_node(git_repository* repo, git_odb* odb, const git_oid* oid) {
  git_odb_object* object = nullptr;
  int error = git_odb_read(&object, odb, oid);
  std::string id = git_oid_tostr_s(oid);
  if (error < 0) {
    std::cerr << "Error reading object with OID " << id << std::endl;
    git_odb_free(odb);
    git_repository_free(repo);
    exit(1);
  }
  git_object_t object_type = git_odb_object_type(object);
  const char* type = git_object_type2string(object_type);
  size_t size = git_odb_object_size(object);

  const char* content = (char*) git_odb_object_data(object);
  std::string string_content(content);
  // Lookup tree for content
  if (type == "tree") {
    git_tree* tree = nullptr;
    if (git_tree_lookup(&tree, repo, oid) < 0) {
      std::cerr << "Failed to lookup tree: " << git_error_last()->message << std::endl;
      git_odb_free(odb);
      git_repository_free(repo);
      exit(1);
    }
    const git_tree_entry *entry;
    size_t i, count;
    string_content = "";
    for (i = 0, count = git_tree_entrycount(tree); i < count; i++) {
      entry = git_tree_entry_byindex(tree, i);
      char buffer[size];
      sprintf(buffer, "%06o %s %s\t%s\n",
                 git_tree_entry_filemode_raw(entry),
                 git_object_type2string(git_tree_entry_type(entry)),
                 git_oid_tostr_s(git_tree_entry_id(entry)),
                 git_tree_entry_name(entry));
      std::string buffer_string = buffer;
      string_content += buffer_string;
    }
  }

  std::cout << id.substr(0, 10) << " " << std::setw(6) << type << " " << size << std::endl;
  graph::node node({id, type, size, string_content});
  git_odb_object_free(object);
  return node;
}

std::vector<graph::edge> get_edges(graph::node node) {
  std::vector<graph::edge> edges = {};
  if (node.type == "commit") {
    // Commit contents start with the following:
    // tree dfea9995ef759d90b879ce623ec9b26f2a781e0c
    //
    // And it'll have a parent commit if not the root-commit:
    // parent 96b5e3f3aaadc5e1e6d6e1510c32c8666db98b51
    // Assume 40-character SHA1 hashes
    std::string treeId = node.content.substr(5, 40);
    
    graph::edge edge({node.id + treeId, node.id, treeId});
    edges.push_back(edge);

    std::string potentialParent = node.content.substr(46, 6);
    if (potentialParent == "parent") {
      std::string parentId = node.content.substr(53, 40);
      graph::edge edge({node.id + parentId, node.id, parentId});
      edges.push_back(edge);
    }
  }
  if (node.type == "tree") {
    // Tree contents are the following:
    // 100644 blob a906cb2a4a904a152e80877d4088654daad0c859      README
    // 100644 blob 8f94139338f9404f26296befa88755fc2598c289      Rakefile
    // 040000 tree 99f1a6d12cb4b6f19c8655fca46c3ecf317074e0      lib
    std::stringstream stream(node.content);
    std::string line;

    while (std::getline(stream, line)) {
      std::string target = line.substr(12, 40);
      graph::edge edge({node.id + target, node.id, target});
      edges.push_back(edge);
    }
  }
  return edges;
}

struct args {
  git_repository* repo;
  git_odb* odb;
  std::shared_ptr<WsClient::Connection> connection;
};

int send_object(const git_oid* oid, void* payload) {
  args* args_pointer = (args*) payload;
  git_repository* repo = args_pointer->repo;
  git_odb* odb = args_pointer->odb;
  std::shared_ptr<WsClient::Connection> connection = args_pointer->connection;
  graph::node node = get_node(repo, odb, oid);
  connection->send(boost::json::serialize(boost::json::value_from(node)));
  std::vector<graph::edge> edges = get_edges(node);
  for (graph::edge edge : edges) {
    connection->send(boost::json::serialize(boost::json::value_from(edge)));
  }
  // Return 0 to continue iterating.
  return 0;
}

class UpdateListener : public efsw::FileWatchListener {
  private:
    git_repository* repo;
    git_odb* odb;
    std::shared_ptr<WsClient::Connection> connection;

  public:
    UpdateListener(git_repository* _repo,
                   git_odb* _odb,
                   std::shared_ptr<WsClient::Connection> _connection) {
      repo = _repo;
      odb = _odb;
      connection = _connection;

      // Read all existing objects in the object database
      args payload = {repo, odb, connection};
      if (git_odb_foreach(odb, send_object, &payload) != 0) {
        std::cerr << "Failed to iterate over object database: " << git_error_last()->message << std::endl;
        git_odb_free(odb);
        git_repository_free(repo);
        git_libgit2_shutdown();
        exit(1);
      }
    }

    void handleFileAction(efsw::WatchID watchid, const std::string& dir,
                          const std::string& filename, efsw::Action action,
                          std::string oldFilename) override {
      // Print event
      std::string action_label = map_action_to_string(action);
      std::cout << action_label << " " << dir << " " << filename;
      if (!oldFilename.empty()) {
        std::cout << " -> " << oldFilename;
      }
      std::cout << std::endl;

      // Handle git object files
      // Assume 40-character SHA1 hashes
      if (filename.size() == 38 && is_hexadecimal(filename)) {
        std::string last_two_chars_excluding_trailing_slash = dir.substr(dir.size() - 3, 2);
        std::string sha1 = last_two_chars_excluding_trailing_slash + filename;
        git_oid oid;
        int error = git_oid_fromstr(&oid, sha1.c_str());
        if (error < 0) {
          std::cerr << "Failed to convert " << sha1 << " to git_oid!" << std::endl;
          return;
        }
        graph::node node = get_node(repo, odb, &oid);
        connection->send(boost::json::serialize(boost::json::value_from(node)));
      }
    }
};

int main(int argc, char* argv[]) {
  if (argc > 2) {
    std::filesystem::path repo_path(argv[1]);
    std::string websocket_host_port_path = argv[2];

    // Initialize libgit2
    if (git_libgit2_init() < 0) {
      std::cerr << "Failed to initialize libgit2." << std::endl;
      return 1;
    }

    // Open repository.
    git_repository* repo = nullptr;
    int code = git_repository_open(&repo, repo_path.string().c_str());
    if (code < 0) {
      const git_error* e = git_error_last();
      std::cerr << "Error opening repository: " << e->message << std::endl;
      git_libgit2_shutdown();
      return 1;
    }

    // Open object database.
    git_odb* odb = nullptr;
    std::string objects_dir = repo_path / ".git" / "objects";
    if (git_odb_open(&odb, objects_dir.c_str()) != 0) {
      std::cerr << "Failed to open object database: " << git_error_last()->message << std::endl;
      git_repository_free(repo);
      git_libgit2_shutdown();
      return 1;
    }

    // WebSocket client code adapted from the following example:
    // https://gitlab.com/eidheim/Simple-WebSocket-Server/-/blob/v2.0.2/ws_examples.cpp?ref_type=tags#L109-146
    WsClient client(websocket_host_port_path);

    client.on_open = [&repo, &odb, &websocket_host_port_path, &objects_dir](std::shared_ptr<WsClient::Connection> connection) {
      std::cout << "Connected to ws://" << websocket_host_port_path << std::endl;
      efsw::FileWatcher* fileWatcher = new efsw::FileWatcher();
      UpdateListener* listener = new UpdateListener(repo, odb, connection);
      bool recursive = true;
      efsw::WatchID watchID = fileWatcher->addWatch(objects_dir, listener, recursive);
      if (watchID < 0) {
        std::cout << "Error " << watchID << " watching directory " << objects_dir << std::endl;
        std::cout << "See https://github.com/SpartanJ/efsw/blob/1.4.1/include/efsw/efsw.h#L76-L85 for error code." << std::endl;
        exit(1);
      }
      fileWatcher->watch(); // Non-blocking
      std::cout << "[Watch #" << watchID  << "] Watching " + objects_dir + " for changes." << std::endl;
    };

    client.on_close = [&websocket_host_port_path](std::shared_ptr<WsClient::Connection> /*connection*/, int status, const std::string & /*reason*/) {
      std::cout << "Closed connection to ws://" << websocket_host_port_path << " with status code " << status << std::endl;
    };

    // See http://www.boost.org/doc/libs/1_55_0/doc/html/boost_asio/reference.html, Error Codes for error code meanings
    client.on_error = [](std::shared_ptr<WsClient::Connection> /*connection*/, const SimpleWeb::error_code &ec) {
      std::cout << "Error: " << ec << ", error message: " << ec.message() << std::endl;
    };

    std::cout << "Connecting to ws://" << websocket_host_port_path << std::endl;
    client.start(); // Block until connection is closed or errors.

    std::cout << "Freeing resources" << std::endl;
    git_odb_free(odb);
    git_repository_free(repo);
    git_libgit2_shutdown();
  } else {
    std::cout << "Usage: <repo_dir> <websocket_host_port_path>" << std::endl;
    std::cout << "    repo_dir - git repository directory to recursively watch for changes" << std::endl;
    std::cout << "    websocket_host_port_path - host, port and path of WebSocket server to write changes to." << std::endl;
    std::cout << "    Example: ./my-repo localhost:8080" << std::endl;
  }
  
}

