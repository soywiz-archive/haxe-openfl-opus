package opus;
import haxe.io.Bytes;
import flash.utils.ByteArray;
import flash.media.Sound;
import sys.FileSystem;
import sys.io.File;

#if neko
import neko.Lib;
#else
import cpp.Lib;
#end

class Opus {
	public static function getVersion():String {
		return hx_opus_get_version_string();
	}
	
	public static function decode(bytes:Bytes):Sound {
		var sound:Sound = new Sound();
		var bytesPerSample:Int = 2;
		var channels:Int = 2;
		var rate:Int = 44100;
		var bytes:Bytes = Bytes.ofData(hx_opus_decode_all(bytes.getData(), rate));
		sound.loadPCMFromByteArray(ByteArray.fromBytes(bytes), Std.int(bytes.length / (bytesPerSample * channels)), "short", (channels == 2) ? true : false, rate);
		//trace(v);
		return sound;
	}
	
	static var hx_opus_get_version_string = Lib.load("openfl-opus", "hx_opus_get_version_string", 0);
	static var hx_opus_decode_all = Lib.load("openfl-opus", "hx_opus_decode_all", 2);
	
}