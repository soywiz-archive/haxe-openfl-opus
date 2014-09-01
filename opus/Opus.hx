package opus;
import haxe.io.Bytes;
import flash.utils.ByteArray;
import flash.media.Sound;
import openfl.events.SampleDataEvent;
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
		return sound;
	}
	
	/*
	public static function decodeStream(bytes:Bytes):Sound {
		var sound = new Sound();
		//var stream = hx_opus_open(bytes.getData(), 44100);
		sound.addEventListener(SampleDataEvent.SAMPLE_DATA, function(e:SampleDataEvent) {
			//e.data.writeBytes(hx_opus_decode(stream));
			for (n in 0 ... 8192) {
				e.data.writeFloat(0);
				e.data.writeFloat(0);
			}
		});
		sound.play();
		return sound;
	}
	*/
	
	static var hx_opus_get_version_string = Lib.load("openfl-opus", "hx_opus_get_version_string", 0);
	static var hx_opus_decode_all = Lib.load("openfl-opus", "hx_opus_decode_all", 2);
	
	//static var hx_opus_open = Lib.load("openfl-opus", "hx_opus_open", 2);
	//static var hx_opus_decode = Lib.load("openfl-opus", "hx_opus_decode", 1);
}
