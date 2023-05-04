# About

Reheat is a back-end for the [Heat](https://github.com/scottgarner/Heat/wiki/) Twitch extension. Heat allows users to click on the stream's video feed and the mouse click is forwarded to the broadcaster. Reheat forwards that information from the Heat servers to a specified window, essentially allowing users to directly interact with the window.

Reheat is intended to allow viewers to interact with a streamer's game remotely, but can be used to forward mouse clicks to any window.

# Usage

Reheat is a command line tool (currently only for Windows). Launch it by providing the target window you want mouse clicks forwarded to as the first parameter and the ID number of your channel as the second. You channel's ID number can be obtained from the Heat extension's configuration page (once activated) in Twitch's extension manager.

```
Reheat.exe [game window title] [channel ID]
```
