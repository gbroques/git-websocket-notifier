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

boost::json::object get_object(git_odb* odb, const git_oid* oid) {
  git_odb_object* object = nullptr;
  int error = git_odb_read(&object, odb, oid);
  std::string id = git_oid_tostr_s(oid);
  if (error < 0) {
    std::cerr << "Error reading object with OID " << id << std::endl;
    return 1;
  }
  git_object_t object_type = git_odb_object_type(object);
  const char* type = git_object_type2string(object_type);
  size_t size = git_odb_object_size(object);
  const char* content = (const char*) git_odb_object_data(object);
  std::cout << id.substr(0, 10) << " " << std::setw(6) << type << " " << size << std::endl;
  boost::json::object obj({
    {"id", id},
    {"type", type},
    {"size", size},
    {"content", content}
  });
  git_odb_object_free(object);
  return obj;
}

struct args {
  git_odb* odb;
  std::shared_ptr<WsClient::Connection> connection;
};

int send_object(const git_oid* oid, void* payload) {
  args* args_pointer = (args*) payload;
  git_odb* odb = args_pointer->odb;
  std::shared_ptr<WsClient::Connection> connection = args_pointer->connection;
  boost::json::object obj = get_object(odb, oid);
  std::string json = boost::json::serialize(obj);
  connection->send(json);
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
      args payload = {odb, connection};
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
      if (filename.size() == 38 && is_hexadecimal(filename)) {
        std::string last_two_chars_excluding_trailing_slash = dir.substr(dir.size() - 3, 2);
        std::string sha1 = last_two_chars_excluding_trailing_slash + filename;
        git_oid oid;
        int error = git_oid_fromstr(&oid, sha1.c_str());
        if (error < 0) {
          std::cerr << "Failed to convert " << sha1 << " to git_oid!" << std::endl;
          return;
        }
        boost::json::object obj = get_object(odb, &oid);
        connection->send(boost::json::serialize(obj));
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

