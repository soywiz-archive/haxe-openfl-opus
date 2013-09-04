OPUS
====

Opus should improve the quality/bitrate of mp3, vorbis and speex codecs.
It could be used to reduce the size of music and speech dialogs in mobiles games.

Information about opus:

* http://www.opus-codec.org/

Setup/Installing
================

You need HAXE and OPENFL. http://www.openfl.org/

```
haxelib install openfl-opus
```


Usage/API
=========

The Opus API is pretty simple. It uses ByteArray and Sound objects for decoding.

```as3
opus.Opus.getVersion():String;
opus.Opus.decode(bytes:Bytes):Sound;
```

Simple example
==============

```as3
trace('Opus version: ' + Opus.getVersion());
var opusBytes:ByteArray = Assets.getBytes("assets/sample.opus");
var sound:Sound = Opus.decode(opusBytes);
sound.play();
```
