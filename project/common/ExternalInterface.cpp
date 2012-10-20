#ifndef IPHONE
#define IMPLEMENT_API
#endif

#undef inline

//#include <hx/CFFI.h>

//#include <Object.h>

#include <hxcpp.h>
//#include <hx/Macros.h>
#include <hx/CFFI.h>
//#include <hx/CFFIAPI.h>
#include <hxcpp.h>
#include "../opus/include/opus.h"
#include "../opus/include/opus_multistream.h"
#include "../ogg/include/ogg/ogg.h"
#include <string.h>
#include <stdio.h>

extern "C" {
#include "../opus_tools/opus_header.h"
#include "../opus_tools/speex_resampler.h"
#include "../opus_tools/wav_io.h"
}

#define DEFINE_FUNC(COUNT, NAME, ...) value NAME(__VA_ARGS__); DEFINE_PRIM(NAME, COUNT); value NAME(__VA_ARGS__)
#define DEFINE_FUNC_0(NAME) DEFINE_FUNC(0, NAME)
#define DEFINE_FUNC_1(NAME, PARAM1) DEFINE_FUNC(1, NAME, value PARAM1)
#define DEFINE_FUNC_2(NAME, PARAM1, PARAM2) DEFINE_FUNC(2, NAME, value PARAM1, value PARAM2)
#define DEFINE_FUNC_3(NAME, PARAM1, PARAM2, PARAM3) DEFINE_FUNC(3, NAME, value PARAM1, value PARAM2, value PARAM3)

#ifdef HAVE_LRINTF
# define float2int(x) lrintf(x)
#else
# define float2int(flt) ((int)(floor(.5+flt)))
#endif

#ifndef HAVE_FMINF
# define fminf(_x,_y) ((_x)<(_y)?(_x):(_y))
#endif

#ifndef HAVE_FMAXF
# define fmaxf(_x,_y) ((_x)>(_y)?(_x):(_y))
#endif


#if defined(HX_WINDOWS) || defined(HX_MACOS) || defined(HX_LINUX)
// Include neko glue....
#define NEKO_COMPATIBLE
#endif

#ifdef HX_WINDOWS
#define snprintf _snprintf
#endif

/* 120ms at 48000 */
#define MAX_FRAME_SIZE (960*6)

struct MemoryStream {
	char *start;
	char *current;
	char *end;
};

void fmemopen(MemoryStream *stream, char *start, int len) {
	stream->current = stream->start = start;
	stream->end = stream->start + len;
}

int fmemeof(MemoryStream *stream) {
	return (stream->current >= stream->end) ? 1 : 0;
}

int fmemread(char *data, int read, MemoryStream *stream) {
	int left = stream->end - stream->current;
	if (read > left) read = left;
	memcpy(data, stream->current, read);
	stream->current += read;
	return read;
}

/*Process an Opus header and setup the opus decoder based on it.
  It takes several pointers for header values which are needed
  elsewhere in the code.*/
static OpusMSDecoder *process_header(
	ogg_packet *op, opus_int32 *rate,
	int *mapping_family, int *channels, int *preskip, float *gain,
	float manual_gain, int *streams, int wav_format, int quiet
)
{
   int err;
   OpusMSDecoder *st;
   OpusHeader header;

   if (opus_header_parse(op->packet, op->bytes, &header)==0)
   {
      fprintf(stderr, "Cannot parse header\n");
      return NULL;
   }

   *mapping_family = header.channel_mapping;
   *channels = header.channels;

   if(!*rate)*rate=header.input_sample_rate;
   /*If the rate is unspecified we decode to 48000*/
   if(*rate==0)*rate=48000;
   if(*rate<8000||*rate>192000){
     fprintf(stderr,"Warning: Crazy input_rate %d, decoding to 48000 instead.\n",*rate);
     *rate=48000;
   }

   *preskip = header.preskip;
   st = opus_multistream_decoder_create(48000, header.channels, header.nb_streams, header.nb_coupled, header.stream_map, &err);
   if(err != OPUS_OK){
     fprintf(stderr, "Cannot create encoder: %s\n", opus_strerror(err));
     return NULL;
   }
   if (!st)
   {
      fprintf (stderr, "Decoder initialization failed: %s\n", opus_strerror(err));
      return NULL;
   }

   *streams=header.nb_streams;

   if(header.gain!=0 || manual_gain!=0)
   {
      /*Gain API added in a newer libopus version, if we don't have it
        we apply the gain ourselves. We also add in a user provided
        manual gain at the same time.*/
      int gainadj = (int)(manual_gain*256.)+header.gain;
#ifdef OPUS_SET_GAIN
      err=opus_multistream_decoder_ctl(st,OPUS_SET_GAIN(gainadj));
      if(err==OPUS_UNIMPLEMENTED)
      {
#endif
         *gain = pow(10., gainadj/5120.);
#ifdef OPUS_SET_GAIN
      } else if (err!=OPUS_OK)
      {
         fprintf (stderr, "Error setting gain: %s\n", opus_strerror(err));
         return NULL;
      }
#endif
   }

   if (!quiet)
   {
      fprintf(stderr, "Decoding to %d Hz (%d channel%s)", *rate,
        *channels, *channels>1?"s":"");
      if(header.version!=1)fprintf(stderr, ", Header v%d",header.version);
      fprintf(stderr, "\n");
      if (header.gain!=0)fprintf(stderr,"Playback gain: %f dB\n", header.gain/256.);
      if (manual_gain!=0)fprintf(stderr,"Manual gain: %f dB\n", manual_gain);
   }

   return st;
}

SpeexResamplerState *resampler=NULL;

opus_int64 audio_write(buffer _buf, float *pcm, int channels, int frame_size, int *skip, opus_int64 maxout)
{	
	int write_samples = channels * frame_size;
	short *temp = (short *)alloca(sizeof(short) * write_samples);
	for (int n = 0; n < write_samples; n++) temp[n] = (short)(pcm[n] * 0x7FFF);
	buffer_append_sub(_buf, (char *)temp, sizeof(short) * write_samples);
	return frame_size;
}

int decode(buffer buf, MemoryStream *fin)
{
   int c;
   int option_index = 0;
   float *output;
   int frame_size=0;
   OpusMSDecoder *st=NULL;
   opus_int64 packet_count=0;
   int total_links=0;
   int stream_init = 0;
   int quiet = 0;
   ogg_int64_t page_granule=0;
   ogg_int64_t link_out=0;
   ogg_sync_state oy;
   ogg_page       og;
   ogg_packet     op;
   ogg_stream_state os;
   int close_in=0;
   int eos=0;
   ogg_int64_t audio_size=0;
   double last_coded_seconds=0;
   float loss_percent=-1;
   float manual_gain=0;
   int channels=-1;
   int mapping_family;
   int rate=48000;
   int wav_format=0;
   int preskip=0;
   int gran_offset=0;
   int has_opus_stream=0;
   ogg_int32_t opus_serialno;
   int dither=1;
   float gain=1;
   int streams=0;
   size_t last_spin=0;


   /*
   if(query_cpu_support()){
     fprintf(stderr,"\n\n** WARNING: This program with compiled with SSE%s\n",query_cpu_support()>1?"2":"");
     fprintf(stderr,"            but this CPU claims to lack these instructions. **\n\n");
   }
   */


   output=0;

   /* .opus files use the Ogg container to provide framing and timekeeping.
    * http://tools.ietf.org/html/draft-terriberry-oggopus
    * The easiest way to decode the Ogg container is to use libogg, so
    *  thats what we do here.
    * Using libogg is fairly straight forward-- you take your stream of bytes
    *  and feed them to ogg_sync_ and it periodically returns Ogg pages, you
    *  check if the pages belong to the stream you're decoding then you give
    *  them to libogg and it gives you packets. You decode the packets. The
    *  pages also provide timing information.*/
   ogg_sync_init(&oy);

   /*Main decoding loop*/
   while (1)
   {
      char *data;
      int i, nb_read;
      /*Get the ogg buffer for writing*/
      data = ogg_sync_buffer(&oy, 200);
      /*Read bitstream from input file*/
      nb_read = fmemread(data, 200, fin);
      ogg_sync_wrote(&oy, nb_read);

      /*Loop for all complete pages we got (most likely only one)*/
      while (ogg_sync_pageout(&oy, &og)==1)
      {
         if (stream_init == 0) {
            ogg_stream_init(&os, ogg_page_serialno(&og));
            stream_init = 1;
         }
         if (ogg_page_serialno(&og) != os.serialno) {
            /* so all streams are read. */
            ogg_stream_reset_serialno(&os, ogg_page_serialno(&og));
         }
         /*Add page to the bitstream*/
         ogg_stream_pagein(&os, &og);
         page_granule = ogg_page_granulepos(&og);
         /*Extract all available packets*/
         while (ogg_stream_packetout(&os, &op) == 1)
         {
            /*OggOpus streams are identified by a magic string in the initial
              stream header.*/
            if (op.b_o_s && op.bytes>=8 && !memcmp(op.packet, "OpusHead", 8)) {
               if(!has_opus_stream)
               {
                 opus_serialno = os.serialno;
                 has_opus_stream = 1;
                 link_out = 0;
                 packet_count = 0;
                 eos = 0;
                 total_links++;
               } else {
                 fprintf(stderr,"Warning: ignoring opus stream %lld\n",(long long)os.serialno);
               }
            }
            if (!has_opus_stream || os.serialno != opus_serialno)
               break;
            /*If first packet in a logical stream, process the Opus header*/
            if (packet_count==0)
            {
               st = process_header(&op, &rate, &mapping_family, &channels, &preskip, &gain, manual_gain, &streams, wav_format, quiet);
               if (!st) {
                  //quit(1);
				  return -1;
				}

               /*Remember how many samples at the front we were told to skip
                 so that we can adjust the timestamp counting.*/
               gran_offset=preskip;

               /*Setup the memory for the dithered output*/
			   /*
               if(!shapemem.a_buf)
               {
                  shapemem.a_buf=calloc(channels,sizeof(float)*4);
                  shapemem.b_buf=calloc(channels,sizeof(float)*4);
                  shapemem.fs=rate;
               }
			   */
               if(!output) {
				output=(float *)malloc(sizeof(float)*MAX_FRAME_SIZE*channels);
				}
            } else if (packet_count==1)
            {
               //if (!quiet) print_comments((char*)op.packet, op.bytes);
            } else {
               int ret;
               opus_int64 maxout;
               opus_int64 outsamp;
               int lost=0;
               if (loss_percent>0 && 100*((float)rand())/RAND_MAX<loss_percent)
                  lost=1;

               /*End of stream condition*/
               if (op.e_o_s && os.serialno == opus_serialno)eos=1; /* don't care for anything except opus eos */

               /*Are we simulating loss for this packet?*/
               if (!lost){
                  /*Decode Opus packet*/
                  ret = opus_multistream_decode_float(st, (unsigned char*)op.packet, op.bytes, output, MAX_FRAME_SIZE, 0);
               } else {
                  /*Extract the original duration.
                    Normally you wouldn't have it for a lost packet, but normally the
                    transports used on lossy channels will effectively tell you.
                    This avoids opusdec squaking when the decoded samples and
                    granpos mismatches.*/
                  opus_int32 lost_size;
                  lost_size = MAX_FRAME_SIZE;
                  if(op.bytes>0){
                    opus_int32 spp;
                    spp=opus_packet_get_nb_frames(op.packet,op.bytes);
                    if(spp>0){
                      spp*=opus_packet_get_samples_per_frame(op.packet,48000/*decoding_rate*/);
                      if(spp>0)lost_size=spp;
                    }
                  }
                  /*Invoke packet loss concealment.*/
                  ret = opus_multistream_decode_float(st, NULL, 0, output, lost_size, 0);
               }

               if(!quiet){
                  /*Display a progress spinner while decoding.*/
                  static const char spinner[]="|/-\\";
                  double coded_seconds = (double)audio_size/(channels*rate*sizeof(short));
                  if(coded_seconds>=last_coded_seconds+1){
                     fprintf(stderr,"\r[%c] %02d:%02d:%02d", spinner[last_spin&3],
                             (int)(coded_seconds/3600),(int)(coded_seconds/60)%60,
                             (int)(coded_seconds)%60);
                     fflush(stderr);
                     last_spin++;
                     last_coded_seconds=coded_seconds;
                  }
               }

               /*If the decoder returned less than zero, we have an error.*/
               if (ret<0)
               {
                  fprintf (stderr, "Decoding error: %s\n", opus_strerror(ret));
                  break;
               }
               frame_size = ret;

               /*If we're collecting --save-range debugging data, collect it now.*/
               /*
			   if(frange!=NULL){
                 OpusDecoder *od;
                 opus_uint32 rngs[256];
                 for(i=0;i<streams;i++){
                   ret=opus_multistream_decoder_ctl(st,OPUS_MULTISTREAM_GET_DECODER_STATE(i,&od));
                   ret=opus_decoder_ctl(od,OPUS_GET_FINAL_RANGE(&rngs[i]));
                 }
                 save_range(frange,frame_size*(48000/48000),op.packet,op.bytes,
                            rngs,streams);
               }
			   */

               /*Apply header gain, if we're not using an opus library new
                 enough to do this internally.*/
               if (gain!=0){
                 for (i=0;i<frame_size*channels;i++)
                    output[i] *= gain;
               }

               /*This handles making sure that our output duration respects
                 the final end-trim by not letting the output sample count
                 get ahead of the granpos indicated value.*/
               maxout=((page_granule-gran_offset)*rate/48000)-link_out;
               outsamp=audio_write(buf, output, channels, frame_size, &preskip, 0>maxout?0:maxout);
               link_out+=outsamp;
               audio_size+=sizeof(short)*outsamp*channels;
            }
            packet_count++;
         }

         if(eos)
         {
            has_opus_stream=0;
            if(st)opus_multistream_decoder_destroy(st);
            st=NULL;
         }
      }
      if (fmemeof(fin)) {
         if(!quiet) {
           fprintf(stderr, "\rDecoding complete.        \n");
           fflush(stderr);
         }
         break;
      }
   }

   /*Did we make it to the end without recovering ANY opus logical streams?*/
   if(!total_links)fprintf (stderr, "This doesn't look like a Opus file\n");

   if (stream_init)
      ogg_stream_clear(&os);
   ogg_sync_clear(&oy);

   /*
   if(shapemem.a_buf)free(shapemem.a_buf);
   if(shapemem.b_buf)free(shapemem.b_buf);
   */

   if(output)free(output);

   return 0;
}

extern "C" {
	DEFINE_FUNC_0(hx_opus_get_version_string) {
		return alloc_string(opus_get_version_string());
	}
	
	DEFINE_FUNC_2(hx_opus_decode_all, data_buffer_value, rate_value) {
		MemoryStream fin;
	
		int rate = 44100;
		if (val_is_int(rate_value)) {
			rate = val_int(rate_value);
		}
		//rate_value
		if (!val_is_buffer(data_buffer_value)) {
			val_throw(alloc_string("Expected to be a buffer"));
			return alloc_null();
		}
		buffer data_buffer = val_to_buffer(data_buffer_value);
		fmemopen(&fin, buffer_data(data_buffer), buffer_size(data_buffer));
		buffer buf = alloc_buffer_len(0);
		decode(buf, &fin);

		//printf("[a]\n");
		if (rate != 48000) {
			int bytes_per_sample = sizeof(short) * 2;
			
			char *in_ptr = buffer_data(buf);
			unsigned int  in_len = buffer_size(buf) / bytes_per_sample;
			
			char *out_ptr = new char[in_len * bytes_per_sample];
			unsigned int  out_len = in_len;

			int err = 0;
			SpeexResamplerState *resampler = speex_resampler_init(2, 48000, rate, 0, &err);
			//printf("[b] %d\n", err);
			//printf("%p, %p, %p, %d, %d\n", resampler, in_ptr, out_ptr, in_len, out_len);
			speex_resampler_process_interleaved_int(
				resampler, 
				(short *)in_ptr,
				&in_len,
				(short *)out_ptr,
				&out_len
			);
			//printf("[c]\n");
			speex_resampler_destroy(resampler);
			
			buffer_set_size(buf, 0);
			buffer_append_sub(buf, out_ptr, out_len * bytes_per_sample);

			//printf("[d]\n");

			delete out_ptr;
		}

		//printf("[e]\n");
		//char* data = buffer_data(buf);

		return buffer_val(buf);
	}
}