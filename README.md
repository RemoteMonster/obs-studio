# Remon OBS Studio project
Remon OBS Studio, and abbreviated ROS is a program that broadcasts using WebRTC and is based on [obs-studio](https://github.com/obsproject/obs-studio) proejct and [Pion WebRTC](https://github.com/pion/webrtc) project. With ROS, you can send live stream to [RemoteMonster](http://www.remotemonster.com) with same user experience as OBS, but also apply to various WebRTC live streaming usecases by modifying the source code.

![](picture.png?raw=true)

To implement ROS, remon_service and remon_output have been added to the outputs plugin of the OBS project. In addition, some UI designs required modifications.

We implemented the WebRTC peerConnection and signaling processing with Go language and used the Pion project for media streaming.

Thanks to the [OBS](https://obsproject.com) and [Pion](https://pion.ly) team.

## Status

Currently, this ROS project is experimental. Limited use only.

## Usage

![](capture1.png?raw=true)

- Select the 'RemoteMonster' service on "Settings" > "Stream".
- Input your service ID and Key from RemoteMonster.


## Installation

- Prebuilt binary is not yet available.


## Build

Note : **It only supports Linux 64bit and Windows 64bit builds**

```sh
git clone --recursive https://github.com/RemoteMonster/remon-obs-studio.git
```

You can build the project with the [Install-Instruction page on the OBS project](https://github.com/obsproject/obs-studio/wiki/Install-Instructions). And you can find the shared library from deps/libremonobs directory.

```
Make sure that the libremonobs.dll(or so file) file exists in the same location as the executable file after build. If not, you should copy it from the source deps / libremonobs / directory.
```
To build libremonobs yourself, check [libremonobs](https://github.com/RemoteMonster/remon-obs-lib) project.

## Limitations

- Regardless of the setting or performance of OBS studio, resolution is supported up to 1920 and bandwidth up to 7000 kbps.
Hardware encoders are not yet supported.


## License

GPL



