#include <util/base.h>
#include <util/threading.h>
#include <obs-module.h>
#include "rtmp-stream.h"
#include <obs-internal.h>
#include <media-io/video-io.h>
#include <media-io/video-frame.h>
#include "../../deps/libremonobs/libremonobs.h"

struct rm_output {
	obs_output_t *output;
	obs_encoder_t* vencoder;
	obs_encoder_t* aencoder;
	pthread_t stop_thread;
	bool stop_thread_active;
	uint32_t video_width;
	uint32_t video_height;
	video_t *mock_video;
	void* video_csd;
	size_t video_csd_len;
	int has_error;
	bool wait_video;
	void* dll_handle;
};

#define REMON_MAX_WH 1920
#define REMON_CHANNEL_NAME "obs-studio"
#define REMON_MAX_VIDEO_BITRATE 7000

#define remon_log(level, format, ...) \
	blog(level, "[Remon] " format, ##__VA_ARGS__)

#define LOGE(format, ...) remon_log(LOG_ERROR,   format, ##__VA_ARGS__)
#define LOGW(format, ...) remon_log(LOG_WARNING, format, ##__VA_ARGS__)
#define LOGI(format, ...) remon_log(LOG_INFO,    format, ##__VA_ARGS__)
#define LOGD(format, ...) remon_log(LOG_DEBUG,   format, ##__VA_ARGS__)


static const char *rm_output_getname(void *unused)
{
	UNUSED_PARAMETER(unused);
	return "RemoteMonster Output";
}


typedef struct RemonCreateCast_return (*fnRemonCreateCast)(GoString serviceId, GoString serviceKey, GoString channelName, GoInt64 videoPtimeUs, GoInt64 audioPtimeUs);
typedef GoInt (*fnRemonWriteVideo)(GoSlice data, GoUint64 ts);
typedef GoInt (*fnRemonWriteAudio)(GoSlice data, GoUint64 ts);
typedef void (*fnRemonClose)();
typedef char* (*fnRemonLastError)();

fnRemonCreateCast	goRemonCreateCast;
fnRemonWriteVideo	goRemonWriteVideo;
fnRemonWriteAudio	goRemonWriteAudio;
fnRemonClose		goRemonClose;
fnRemonLastError	goRemonLastError;

static GoString char2GoString( char* string) {
	GoString a = {
		.p = string,
		.n = strlen(string),
	};
	return a;
}

static bool rm_init_encoders( struct rm_output *context) {
	obs_encoder_t *def_vencoder = obs_output_get_video_encoder(context->output);
	obs_data_t *def_settings = obs_encoder_get_settings(def_vencoder);
	long long bitrate = obs_data_get_int(def_settings, "bitrate");
	if( bitrate > REMON_MAX_VIDEO_BITRATE) {
		bitrate = REMON_MAX_VIDEO_BITRATE;
	}
	obs_data_release(def_settings);

	uint32_t w = obs_output_get_width( context->output);
	uint32_t h = obs_output_get_height( context->output);
	uint32_t max_wh = w > h ? w : h;
	double ratio = 1.0;
	if( max_wh > REMON_MAX_WH) {
		ratio = (double)REMON_MAX_WH / (double)max_wh;
	}
	context->video_width = w * ratio;
	context->video_height = h * ratio;
	
	LOGD("set output dimension (%u,%u) -> (%u,%u)", w, h, context->video_width, context->video_height);

	// create mock video output
	const struct video_output_info *voi = video_output_get_info(obs_get_video());
	if( voi) {
		struct video_output_info newvoi = {0};
		newvoi.width = context->video_width;
		newvoi.height = context->video_height;
		newvoi.format = VIDEO_FORMAT_NV12;
		newvoi.fps_num = voi->fps_num;
		newvoi.fps_den = voi->fps_den;
		newvoi.cache_size = 16; //16
		newvoi.colorspace = voi->colorspace;
		newvoi.range = voi->range;
		newvoi.name = "remon_mock_video";
		int ret = video_output_open(&context->mock_video, &newvoi);
	} else {
		context->has_error = 1;
		LOGE("######## video_output_get_info failed");
		return false;
	}

	// set video conversion appropriate for webrtc
	if( true) {
		struct video_scale_info to;
		memset(&to, 0, sizeof(struct video_scale_info));
		to.format = VIDEO_FORMAT_NV12;
		to.width = context->video_width;
		to.height = context->video_height;
		obs_output_set_video_conversion( context->output, &to);
	}

	obs_data_t *vencoder_settings = obs_data_create();
	obs_data_t *aencoder_settings = obs_data_create();
	obs_data_set_int   (vencoder_settings, "bitrate",     bitrate);
	obs_data_set_bool  (vencoder_settings, "use_bufsize", false);
	obs_data_set_int   (vencoder_settings, "buffer_size", bitrate);
	obs_data_set_int   (vencoder_settings, "keyint_sec",  2);
	//obs_data_set_int   (vencoder_settings, "crf",         23);
#ifdef ENABLE_VFR
	obs_data_set_bool  (settings, "vfr",         false);
#endif
	obs_data_set_string(vencoder_settings, "rate_control","CBR");
	obs_data_set_string(vencoder_settings, "preset",      "veryfast");
	obs_data_set_string(vencoder_settings, "profile",     "baseline");
	obs_data_set_string(vencoder_settings, "tune",        "zerolatency");
	obs_data_set_string(vencoder_settings, "x264opts",    "");
	obs_data_set_int(   vencoder_settings, "bf", 0);

	obs_data_set_int(aencoder_settings, "bitrate", 128);

	obs_encoder_update(context->vencoder, vencoder_settings);
	obs_encoder_update(context->aencoder, aencoder_settings);

	obs_encoder_set_video(context->vencoder, context->mock_video);
	// use default audio
	obs_encoder_set_audio(context->aencoder, obs_get_audio());

	obs_data_release(vencoder_settings);
	obs_data_release(aencoder_settings);
	return true;
}

static void *rm_output_create(obs_data_t *settings, obs_output_t *output)
{
	LOGD("rm_output_create");
	void* dll_handle = os_dlopen("libremonobs");
	if (dll_handle == NULL) {
		return NULL;
	}
	goRemonCreateCast = (fnRemonCreateCast)os_dlsym(dll_handle, "RemonCreateCast");
	goRemonWriteVideo = (fnRemonWriteVideo)os_dlsym(dll_handle, "RemonWriteVideo");
	goRemonWriteAudio = (fnRemonWriteAudio)os_dlsym(dll_handle, "RemonWriteAudio");
	goRemonClose = (fnRemonClose)os_dlsym(dll_handle, "RemonClose");
	goRemonLastError = (fnRemonLastError)os_dlsym(dll_handle, "RemonLastError");

	struct rm_output *context = bzalloc(sizeof(*context));
	context->dll_handle = dll_handle;
	context->output = output;

	context->vencoder = obs_video_encoder_create("obs_x264", "remon_x264", NULL, NULL);
	context->aencoder = obs_audio_encoder_create("ffmpeg_opus", "remon_opus", NULL, 0, NULL);

	UNUSED_PARAMETER(settings);
	return context;
}

static void rm_output_destroy(void *data)
{
	LOGD("rm_output_destroy");
	struct rm_output *context = data;
	
	if (context->stop_thread_active)
		pthread_join(context->stop_thread, NULL);

	obs_encoder_release(context->vencoder);
	obs_encoder_release(context->aencoder);
	if( context->video_csd)
		bfree( context->video_csd);
	if (context->dll_handle)
		os_dlclose(context->dll_handle);
	bfree(context);
}

static void send_video( void* data, size_t len, uint64_t ts) {
	GoSlice p;
	p.data = data;
	p.len = len;
	p.cap = len;
	goRemonWriteVideo( p, ts);
}

static void send_audio( void* data, size_t len, uint64_t ts) {
	GoSlice p;
	p.data = data;
	p.len = len;
	p.cap = len;
	goRemonWriteAudio( p, ts);
}

static void video_encoded_callback(void *data, struct encoder_packet *packet) {
	struct rm_output *context = data;
	static int first = 1;
	if( first) {
		first = 0;
		uint8_t       *header;
		size_t        size;
		obs_encoder_get_extra_data(context->vencoder, &header, &size);
		if( context->video_csd)
			bfree( context->video_csd);
		context->video_csd = bmemdup( header, size);
		context->video_csd_len = size;
	}

	int64_t tm = packet->pts * 1000 *  packet->timebase_num / packet->timebase_den;
	if( packet->keyframe) {
		if( context->video_csd) {
			send_video( context->video_csd, context->video_csd_len, 0);
		}
	}
	send_video( packet->data, packet->size, tm);
	context->wait_video = false;
}


static void audio_encoded_callback(void *data, struct encoder_packet *packet) {
	struct rm_output *context = data;
	int64_t tm = packet->pts * 1000 *  packet->timebase_num / packet->timebase_den;

	if( !context->wait_video)
		send_audio( packet->data, packet->size, tm);
}

static bool rm_output_start(void *data)
{
	LOGD("rm_output_start");
	struct rm_output *context = data;
	if( context->has_error) {
		return false;
	}
	if (context->stop_thread_active)
		pthread_join(context->stop_thread, NULL);

	obs_service_t *service = obs_output_get_service(context->output);
	if (!service) {
		return false;
	}
	const char* username = obs_service_get_username( service);
	const char* password = obs_service_get_password( service);

	if( !rm_init_encoders( context)) {
		return false;
	}
	int64_t videoPtime, audioPtime;
	audioPtime = 20000000;
	videoPtime = 33333333;
	struct obs_video_info ovi;
	if( obs_get_video_info(&ovi)) {
		LOGD("get_video_info fps: %u / %u", ovi.fps_num, ovi.fps_den);
		videoPtime = 1000000000.0 / (double)ovi.fps_num * (double)ovi.fps_den;
	} else {
		LOGE("obs_get_video_info failed");
		return false;
	}

	context->wait_video = true;

	if (!obs_output_can_begin_data_capture(context->output, 0)) {
		LOGE("obs_output_can_begin_data_capture failed");
		return false;
	}
	if ( !obs_encoder_initialize(context->vencoder)) {
		LOGE("obs_encoder_initialize(video) failed");
		return false;
	}
	if ( !obs_encoder_initialize(context->aencoder)) {
		LOGE("obs_encoder_initialize(audio) failed");
		return false;
	}

	struct RemonCreateCast_return result = goRemonCreateCast(char2GoString(username), char2GoString(password), char2GoString(REMON_CHANNEL_NAME), videoPtime, audioPtime);
	if( result.r2 != 0) {
		char* err_msg = goRemonLastError();
		LOGE("goRemonCreateCast failed %s", err_msg); 
		return false;
	}
	char *ch_id, *token;
	ch_id = result.r0;
	token = result.r1;
	LOGI("peer token : %s, channel id : %s", token, ch_id);
	obs_encoder_start(context->vencoder, video_encoded_callback, context);
	obs_encoder_start(context->aencoder, audio_encoded_callback, context);
	obs_output_begin_data_capture(context->output, 0);
	return true;
}

static void *stop_thread(void *data)
{
	struct rm_output *context = data;
	obs_output_end_data_capture(context->output);
	context->stop_thread_active = false;

	obs_encoder_stop( context->vencoder, video_encoded_callback, context);
	obs_encoder_stop( context->aencoder, audio_encoded_callback, context);
	video_output_close(context->mock_video);

	goRemonClose();

	return NULL;
}

static void rm_output_stop(void *data, uint64_t ts)
{
	LOGD("rm_output_stop");
	struct rm_output *context = data;
	UNUSED_PARAMETER(ts);

	context->stop_thread_active = pthread_create(&context->stop_thread,
			NULL, stop_thread, data) == 0;
}

static void rm_output_defaults(obs_data_t *defaults)
{
	obs_data_set_default_int(defaults, OPT_DROP_THRESHOLD, 700);
	obs_data_set_default_int(defaults, OPT_PFRAME_DROP_THRESHOLD, 900);
	obs_data_set_default_int(defaults, OPT_MAX_SHUTDOWN_TIME_SEC, 30);
	obs_data_set_default_string(defaults, OPT_BIND_IP, "default");
	obs_data_set_default_bool(defaults, OPT_NEWSOCKETLOOP_ENABLED, false);
	obs_data_set_default_bool(defaults, OPT_LOWLATENCY_ENABLED, false);
}

static obs_properties_t *rm_output_properties(void *unused)
{
	UNUSED_PARAMETER(unused);

	obs_properties_t *props = obs_properties_create();
	struct netif_saddr_data addrs = {0};
	obs_property_t *p;

	obs_properties_add_int(props, OPT_DROP_THRESHOLD,
			obs_module_text("RTMPStream.DropThreshold"),
			200, 10000, 100);

	p = obs_properties_add_list(props, OPT_BIND_IP,
			obs_module_text("RTMPStream.BindIP"),
			OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

	obs_property_list_add_string(p, obs_module_text("Default"), "default");

	netif_get_addrs(&addrs);
	for (size_t i = 0; i < addrs.addrs.num; i++) {
		struct netif_saddr_item item = addrs.addrs.array[i];
		obs_property_list_add_string(p, item.name, item.addr);
	}
	netif_saddr_data_free(&addrs);

	obs_properties_add_bool(props, OPT_NEWSOCKETLOOP_ENABLED,
			obs_module_text("RTMPStream.NewSocketLoop"));
	obs_properties_add_bool(props, OPT_LOWLATENCY_ENABLED,
			obs_module_text("RTMPStream.LowLatencyMode"));

	return props;
}

static void rm_raw_video(void *data, struct video_data *frame) {
	struct rm_output *context = data;

	struct video_frame output_frame;
	if(	video_output_lock_frame(context->mock_video, &output_frame, 1, frame->timestamp)) {
		uint8_t *src, *dest;
		src = (uint8_t*)frame->data[0];
		dest = output_frame.data[0];
		for( uint32_t i=0; i < context->video_height; i++) {
			memcpy(dest, src, context->video_width);
			dest += output_frame.linesize[0];
			src += frame->linesize[0];
		}
		src = frame->data[1];
		dest = output_frame.data[1];
		for( uint32_t i=0; i < context->video_height/2; i++) {
			memcpy(dest, src, context->video_width);
			dest += output_frame.linesize[0];
			src += frame->linesize[0];
		}
		video_output_unlock_frame( context->mock_video);
	}
}

static void rm_raw_audio(void *data, struct audio_data *frames) {
	UNUSED_PARAMETER( data);
	UNUSED_PARAMETER( frames);
}

struct obs_output_info rm_output_info = {
	.id                   = "rm_output",
	.flags                = OBS_OUTPUT_AV |
	                        OBS_OUTPUT_SERVICE,
//	.encoded_video_codecs = "h264",
//	.encoded_audio_codecs = "opus",
	.get_name           = rm_output_getname,
	.create             = rm_output_create,
	.destroy            = rm_output_destroy,
	.start              = rm_output_start,
	.stop               = rm_output_stop,
//	.encoded_packet     = rm_output_data,
	.get_defaults         = rm_output_defaults,
	//.get_properties       = rm_output_properties,
	//.get_total_bytes      = rm_output_total_bytes_sent,
	//.get_congestion       = rm_output_congestion,
	//.get_connect_time_ms  = rm_output_connect_time,
	//.get_dropped_frames   = rm_output_dropped_frames
	.raw_video          = rm_raw_video,
	.raw_audio          = rm_raw_audio,
};
