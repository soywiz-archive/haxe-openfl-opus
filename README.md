haxe-openfl-opus
================

OPENFL haxe extensions for decoding opus audio files. 

sample
======

```as3
trace('Opus version: ' + Opus.getVersion());
var opusBytes:ByteArray = Assets.getBytes("assets/sample.opus");
var sound:Sound = Opus.decode(opusBytes);
sound.play();
```
