#include "/usr/lib/oss/include/sys/soundcard.h"
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>

#include "cubeb/cubeb.h"
#include "cubeb-internal.h"


#define OSS_BUFFER_SIZE 2000

struct cubeb {
    struct cubeb_ops const * ops;
};

struct cubeb_stream {
    cubeb * context;
  
    cubeb_data_callback data_callback;
    cubeb_state_callback state_callback;
    void * user_ptr;  
    float volume;
    
    pthread_mutex_t state_mutex;
  
    int running;
    int stopped;
    int floating;
    
    uint64_t position;
    
    cubeb_stream_params params;
    int fd;
    pthread_t th;
};

static struct cubeb_ops const oss_ops;

int oss_init(cubeb ** context, char const * context_name){
    cubeb* ctx = (cubeb*)malloc(sizeof(cubeb));
    ctx->ops = &oss_ops;
    *context = ctx;
    return CUBEB_OK;
}

static void oss_destroy(cubeb *ctx){
    free(ctx);
}

static char const * oss_get_backend_id(cubeb * context){
    static char oss_name[] = "OSS";
    return oss_name;
}

static int oss_get_max_channel_count(cubeb * ctx, uint32_t * max_channels){
    *max_channels = 2; /* Let's support only stereo for now */
    return CUBEB_OK;
}

static int oss_get_min_latency(cubeb * context, cubeb_stream_params params, uint32_t * latency_ms){
    *latency_ms = 40;
    return CUBEB_OK;
}

static int oss_get_preferred_sample_rate(cubeb *context, uint32_t * rate){
    *rate = 48000;
    return CUBEB_OK;
}

static void run_state_callback(cubeb_stream *stream, cubeb_state state){
    pthread_mutex_lock(&stream->state_mutex);
    if(stream->state_callback && stream->running) 
        stream->state_callback(stream, stream->user_ptr, state);
    pthread_mutex_unlock(&stream->state_mutex);
}

static long run_data_callback(cubeb_stream *stream, void *buffer, long nframes){
    pthread_mutex_lock(&stream->state_mutex);
    long got = 0;
    if(stream->data_callback && stream->running && !stream->stopped){
        got = stream->data_callback(stream, stream->user_ptr, buffer, nframes);
    }
    pthread_mutex_unlock(&stream->state_mutex);
    return got;
}

static void *writer(void *stm) {
    cubeb_stream* stream = (cubeb_stream*)stm;
    int16_t buffer[OSS_BUFFER_SIZE];
	while (stream->running) {
        if(stream->stopped){
            run_state_callback(stream, CUBEB_STATE_STOPPED);
            while(stream->stopped) usleep(10000); /* This better be done with conditionals */
            run_state_callback(stream, CUBEB_STATE_STARTED);
        }
            
        if(!stream->running) break;
        int got;
        if(stream->floating){
            float f_buffer[OSS_BUFFER_SIZE];
            got = stream->data_callback(stream, stream->user_ptr, f_buffer, OSS_BUFFER_SIZE/stream->params.channels);
            int i;
            for(i=0; i<got*stream->params.channels; i++){
                buffer[i] = f_buffer[i]*30000.0*stream->volume;
            }
        } else
            got = stream->data_callback(stream, stream->user_ptr, buffer, OSS_BUFFER_SIZE/stream->params.channels);
        if(!got){
            run_state_callback(stream, CUBEB_STATE_DRAINED);
        }
		size_t i = 0;
        size_t s = got*stream->params.channels*sizeof(int16_t);
		while (i < s) {
			i += write(stream->fd, ((char*)buffer) + i, s - i);
		}
        stream->position+=got;
	}
    close(stream->fd);
    free(stream);
	return NULL;
}

static int oss_stream_init(cubeb * context, cubeb_stream ** stm, char const * stream_name,
                           cubeb_stream_params stream_params, unsigned int latency,
                           cubeb_data_callback data_callback,
                           cubeb_state_callback state_callback,
                           void * user_ptr){
    cubeb_stream* stream = (cubeb_stream*)malloc(sizeof(cubeb_stream));
    stream->context = context;
    stream->data_callback = data_callback;
    stream->state_callback = state_callback;
    stream->user_ptr = user_ptr;
    
    if ((stream->fd = open("/dev/dsp", O_WRONLY)) == -1) {
        return CUBEB_ERROR;
	}
    int i, j;
#define SET(what, to) i = to; \
	j = ioctl(stream->fd, what, &i); \
	if (j == -1) \
		printf("failed to set %s to %i", #what, i); 
    
    stream->params = stream_params;
    stream->volume = 1;
    
    pthread_mutex_init(&stream->state_mutex, NULL);
    
    stream->floating = 0;
    SET(SNDCTL_DSP_CHANNELS, stream_params.channels);
    SET(SNDCTL_DSP_SPEED, stream_params.rate);
    switch(stream_params.format){
        case CUBEB_SAMPLE_S16LE: SET(SNDCTL_DSP_SETFMT, AFMT_S16_LE); break;
        case CUBEB_SAMPLE_S16BE: SET(SNDCTL_DSP_SETFMT, AFMT_S16_BE); break;
        case CUBEB_SAMPLE_FLOAT32LE: SET(SNDCTL_DSP_SETFMT, AFMT_S16_NE); stream->floating = 1; break;
        default: return CUBEB_ERROR;
    }
    
    stream->running = 1;
    stream->stopped = 1;
    stream->position = 0;
    
    pthread_create(&stream->th, NULL, writer, (void*)stream);
	pthread_detach(stream->th);
    
    if(stream->state_callback) stream->state_callback(stream, stream->user_ptr, CUBEB_STATE_STARTED);
    
    *stm = stream;
    
    return CUBEB_OK;
}

static void oss_stream_destroy(cubeb_stream * stream){
    pthread_mutex_lock(&stream->state_mutex);
    /*stream->state_callback = NULL;*/
    stream->running = 0;
    stream->stopped = 0;
    pthread_mutex_unlock(&stream->state_mutex);
    pthread_join(stream->th, NULL);
    pthread_mutex_destroy(&stream->state_mutex);
}


static int oss_stream_start(cubeb_stream * stream){
    pthread_mutex_lock(&stream->state_mutex);
    stream->stopped = 0;
    pthread_mutex_unlock(&stream->state_mutex);
    return CUBEB_OK;
}

static int oss_stream_stop(cubeb_stream * stream){
    pthread_mutex_lock(&stream->state_mutex);
    stream->stopped = 1;
    pthread_mutex_unlock(&stream->state_mutex);
    return CUBEB_OK;
}

static int oss_stream_get_latency(cubeb_stream * stream, uint32_t * latency){
    /* Just set it to smth for now */
    *latency = 10;
    return CUBEB_OK;
}

static int oss_stream_get_position(cubeb_stream * stream, uint64_t * position){
    *position = stream->position;
    return CUBEB_OK;
}

int oss_stream_set_panning(cubeb_stream * stream, float panning){
    return CUBEB_OK;
}

int oss_stream_set_volume(cubeb_stream * stream, float volume){
    stream->volume=volume;
    return CUBEB_OK;
}

static struct cubeb_ops const oss_ops = {
  .init = oss_init, 
  .get_backend_id = oss_get_backend_id,
  .get_max_channel_count = oss_get_max_channel_count,
  /*.get_min_latency = oss_get_min_latency,
  .get_preferred_sample_rate = oss_get_preferred_sample_rate,*/
  .destroy = oss_destroy,
  .stream_init = oss_stream_init,
  .stream_destroy = oss_stream_destroy, 
  .stream_start = oss_stream_start,
  .stream_stop = oss_stream_stop,
  .stream_get_position = oss_stream_get_position /*,
  .stream_get_latency = oss_stream_get_latency,
  .stream_set_volume = oss_stream_set_volume,
  .stream_set_panning = oss_stream_set_panning,  
  .stream_get_current_device = NULL,
  .stream_device_destroy = NULL,
  .stream_register_device_changed_callback = NULL*/
};
