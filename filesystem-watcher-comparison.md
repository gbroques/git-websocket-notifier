# Library Comparison

* [dmon](https://github.com/septag/dmon) - doesn't have a way to differentiate between files and directories (see [issue #35](https://github.com/septag/dmon/issues/35)).
* [fswatch](https://github.com/emcrisostomo/fswatch) - requires filters for removing noisy inotify `PlatformSpecific` events, and more critically doesn't detect object file creation during recursive monitorying (e.g doesn't detect creation of `.git/objects/56/26abf0f72e58d7a153368ba57db4c673c0e171`). `inotifywait` detects this, but has additional code around `inotify` to do so (see [issue #330](https://github.com/emcrisostomo/fswatch/issues/330)).
* [efsw](https://github.com/SpartanJ/efsw) - doesn't have a way to differentiate between files and directories (see [issue #191](https://github.com/SpartanJ/efsw/issues/191)).
* [Node.js fs.watch](https://nodejs.org/docs/latest/api/fs.html#fswatchfilename-options-listener) - only reports `'rename'` or `'change'` `eventType`s instead of create, delete, and update, and move. It also has caveats, and triggered wrapper projects such as [`node-watch`](https://github.com/yuanchuan/node-watch) and [`chokidar`](https://github.com/paulmillr/chokidar).

Both `fswatch` and Node.js `fs.watch` have issues that make them unworkable.

`dmon` and `efswatch` are workable since we can detect files between directories for Git based on names, length, and other attributes.

`efswatch` would be preferable since it's in Conan center, written in C++ as opposed to C, and seems a bit more popular and maintained.

## git add Comparison

Below is a comparison of output for the following command:
```
$ git add file
```
Where `file` is a plain-text file with the contents "`file`":
```bash
$ cat file
file
```

`efsw` output:
```
DIR (.git/) FILE (index.lock) has event Added
DIR (.git/objects/) FILE (f7) has event Added
DIR (.git/objects/f7/) FILE (tmp_obj_JUp4eR) has event Modified
DIR (.git/objects/f7/) FILE (tmp_obj_JUp4eR) has event Modified
DIR (.git/objects/f7/) FILE (3f3093ff865c514c6c51f867e35f693487d0d3) has event Added
DIR (.git/objects/f7/) FILE (tmp_obj_JUp4eR) has event Delete
DIR (.git/) FILE (index.lock) has event Modified
DIR (.git/) FILE (index.lock) has event Modified
DIR (.git/) FILE (index.lock) has event Modified
DIR (.git/) FILE (index) has event Moved from (index.lock)
````

`dmon` output:
```
{"filepath":".git/objects/f7","action":"CREATE"}
{"filepath":".git/index","action":"MODIFY"}
{"filepath":".git/objects/f7/3f3093ff865c514c6c51f867e35f693487d0d3","action":"CREATE"}
```

`inotifywait` output (used as a source of truth):
```
$ inotifywait -m .git/ -r
.git/ ACCESS HEAD
.git/ CLOSE_NOWRITE,CLOSE HEAD
.git/ OPEN config
.git/ ACCESS config
.git/ CLOSE_NOWRITE,CLOSE config
.git/ OPEN config
.git/ ACCESS config
.git/ CLOSE_NOWRITE,CLOSE config
.git/ OPEN config
.git/ ACCESS config
.git/ CLOSE_NOWRITE,CLOSE config
.git/ CREATE index.lock
.git/ OPEN index.lock
.git/info/ OPEN exclude
.git/info/ ACCESS exclude
.git/info/ CLOSE_NOWRITE,CLOSE exclude
.git/objects/ OPEN,ISDIR pack
.git/objects/pack/ OPEN,ISDIR 
.git/objects/ ACCESS,ISDIR pack
.git/objects/pack/ ACCESS,ISDIR 
.git/objects/ ACCESS,ISDIR pack
.git/objects/pack/ ACCESS,ISDIR 
.git/objects/ CLOSE_NOWRITE,CLOSE,ISDIR pack
.git/objects/pack/ CLOSE_NOWRITE,CLOSE,ISDIR 
.git/objects/ CREATE,ISDIR f7
.git/objects/ OPEN,ISDIR f7
.git/objects/ ACCESS,ISDIR f7
.git/objects/ CLOSE_NOWRITE,CLOSE,ISDIR f7
.git/objects/f7/ MODIFY tmp_obj_JUp4eR
.git/objects/f7/ CLOSE_WRITE,CLOSE tmp_obj_JUp4eR
.git/objects/f7/ CREATE 3f3093ff865c514c6c51f867e35f693487d0d3
.git/objects/f7/ DELETE tmp_obj_JUp4eR
.git/ MODIFY index.lock
.git/ CLOSE_WRITE,CLOSE index.lock
.git/ MOVED_FROM index.lock
.git/ MOVED_TO index
.git/ OPEN HEAD
.git/ ACCESS HEAD
.git/ CLOSE_NOWRITE,CLOSE HEAD
.git/ OPEN config
.git/ ACCESS config
.git/ CLOSE_NOWRITE,CLOSE config
.git/ OPEN config
.git/ ACCESS config
.git/ CLOSE_NOWRITE,CLOSE config
.git/ OPEN HEAD
.git/ ACCESS HEAD
.git/ CLOSE_NOWRITE,CLOSE HEAD
.git/ OPEN HEAD
.git/ ACCESS HEAD
.git/ CLOSE_NOWRITE,CLOSE HEAD
.git/ OPEN HEAD
.git/ ACCESS HEAD
.git/ CLOSE_NOWRITE,CLOSE HEAD
.git/ OPEN config
.git/ ACCESS config
.git/ CLOSE_NOWRITE,CLOSE config
.git/ OPEN config
.git/ ACCESS config
.git/ CLOSE_NOWRITE,CLOSE config
.git/ OPEN config
.git/ ACCESS config
.git/ CLOSE_NOWRITE,CLOSE config
.git/ OPEN HEAD
.git/ ACCESS HEAD
.git/ CLOSE_NOWRITE,CLOSE HEAD
.git/ OPEN HEAD
.git/ ACCESS HEAD
.git/ CLOSE_NOWRITE,CLOSE HEAD
.git/ OPEN config
.git/ ACCESS config
.git/ CLOSE_NOWRITE,CLOSE config
.git/ OPEN config
.git/ ACCESS config
.git/ CLOSE_NOWRITE,CLOSE config
.git/ OPEN config
.git/ ACCESS config
.git/ CLOSE_NOWRITE,CLOSE config
```

`dmon` doesn't detect git creating the `index.lock`, `index` or `tmp_obj_JUp4eR`. These aren't really important, but it's concerning that events are swallowed leading to an inaccurate picture.
