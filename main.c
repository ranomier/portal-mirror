#include <libportal/portal.h>
#include <pipewire/pipewire.h>
#include <spa/param/video/format-utils.h>
#include <SDL3/SDL.h>
#include <glib.h>
#include <string.h>
#include <signal.h>

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    struct pw_thread_loop *loop;
    struct pw_stream *stream;
    int width;
    int height;
    GMainLoop *main_loop;

    GMutex frame_mutex;
    uint8_t *frame_buffer;
    gboolean new_frame;
    gboolean window_created;
    gboolean format_ready;
    gboolean needs_texture_recreate;
} AppData;

static AppData *global_data = NULL;

static void signal_handler(int signum) {
    if (global_data && global_data->main_loop) {
        g_main_loop_quit(global_data->main_loop);
    }
}

static void on_process(void *userdata) {
    AppData *data = userdata;
    struct pw_buffer *b;
    struct spa_buffer *buf;
    uint8_t *src;

    if ((b = pw_stream_dequeue_buffer(data->stream)) == NULL)
        return;

    buf = b->buffer;
    if (buf->datas[0].data == NULL)
        goto done;

    src = buf->datas[0].data;

    g_mutex_lock(&data->frame_mutex);
    if (data->frame_buffer != NULL && !data->needs_texture_recreate) {
        memcpy(data->frame_buffer, src, data->width * data->height * 4);
        data->new_frame = TRUE;
    }
    g_mutex_unlock(&data->frame_mutex);

done:
    pw_stream_queue_buffer(data->stream, b);
}

static void on_param_changed(void *userdata, uint32_t id, const struct spa_pod *param) {
    AppData *data = userdata;

    if (param == NULL || id != SPA_PARAM_Format)
        return;

    struct spa_video_info_raw info;
    if (spa_format_video_raw_parse(param, &info) < 0)
        return;

    g_mutex_lock(&data->frame_mutex);

    int new_width = info.size.width;
    int new_height = info.size.height;

    if (new_width != data->width || new_height != data->height) {
        g_print("Resolution changed: %dx%d -> %dx%d\n", data->width, data->height, new_width, new_height);

        if (data->frame_buffer) {
            g_free(data->frame_buffer);
        }

        data->width = new_width;
        data->height = new_height;
        data->frame_buffer = g_malloc(data->width * data->height * 4);
        data->needs_texture_recreate = TRUE;
        data->format_ready = TRUE;
    } else if (!data->format_ready) {
        data->width = new_width;
        data->height = new_height;
        data->frame_buffer = g_malloc(data->width * data->height * 4);
        data->format_ready = TRUE;
    }

    g_mutex_unlock(&data->frame_mutex);
}

static const struct pw_stream_events stream_events = {
    PW_VERSION_STREAM_EVENTS,
    .param_changed = on_param_changed,
    .process = on_process,
};

static void start_pipewire_stream(AppData *data, uint32_t node_id) {
    struct pw_properties *props;
    uint8_t buffer[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const struct spa_pod *params[1];

    g_print("Starting PipeWire stream for node %u\n", node_id);

    pw_init(NULL, NULL);

    data->loop = pw_thread_loop_new("pipewire-loop", NULL);

    props = pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Video",
        PW_KEY_MEDIA_CATEGORY, "Capture",
        PW_KEY_MEDIA_ROLE, "Screen",
        NULL);

    data->stream = pw_stream_new_simple(
        pw_thread_loop_get_loop(data->loop),
        "portal-mirror",
        props,
        &stream_events,
        data);

    params[0] = spa_pod_builder_add_object(&b,
        SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
        SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video),
        SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
        SPA_FORMAT_VIDEO_format, SPA_POD_Id(SPA_VIDEO_FORMAT_BGRx),
        0);

    pw_stream_connect(data->stream,
                     PW_DIRECTION_INPUT,
                     node_id,
                     PW_STREAM_FLAG_AUTOCONNECT |
                     PW_STREAM_FLAG_MAP_BUFFERS,
                     params, 1);

    pw_thread_loop_start(data->loop);
    g_print("PipeWire thread loop started\n");
}

static void on_session_started(GObject *source, GAsyncResult *result, gpointer user_data) {
    XdpSession *session = XDP_SESSION(source);
    AppData *data = user_data;
    g_autoptr(GError) error = NULL;
    GVariant *streams;
    GVariantIter iter;
    uint32_t node_id;
    GVariant *stream_properties;

    if (!xdp_session_start_finish(session, result, &error)) {
        g_printerr("Failed to start session: %s\n", error->message);
        g_main_loop_quit(data->main_loop);
        return;
    }

    streams = xdp_session_get_streams(session);
    if (!streams) {
        g_printerr("No streams available!\n");
        g_main_loop_quit(data->main_loop);
        return;
    }

    g_variant_iter_init(&iter, streams);

    if (g_variant_iter_next(&iter, "(u@a{sv})", &node_id, &stream_properties)) {
        g_print("Got node ID: %u\n", node_id);
        start_pipewire_stream(data, node_id);
        g_variant_unref(stream_properties);
    } else {
        g_printerr("Failed to get stream info\n");
        g_main_loop_quit(data->main_loop);
    }
}

static void on_screencast_created(GObject *source, GAsyncResult *result, gpointer user_data) {
    XdpPortal *portal = XDP_PORTAL(source);
    AppData *data = user_data;
    g_autoptr(GError) error = NULL;
    XdpSession *session;

    session = xdp_portal_create_screencast_session_finish(portal, result, &error);

    if (error) {
        g_printerr("Error creating session: %s\n", error->message);
        g_main_loop_quit(data->main_loop);
        return;
    }

    xdp_session_start(session, NULL, NULL, on_session_started, data);
}

static gboolean check_sdl_events(gpointer user_data) {
    AppData *data = user_data;
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
            g_main_loop_quit(data->main_loop);
            return G_SOURCE_REMOVE;
        }
        if (event.type == SDL_EVENT_KEY_DOWN) {
            if ((event.key.mod & SDL_KMOD_CTRL) && event.key.key == SDLK_Q) {
                g_main_loop_quit(data->main_loop);
                return G_SOURCE_REMOVE;
            }
        }
    }

    g_mutex_lock(&data->frame_mutex);

    if (data->format_ready && !data->window_created) {
        data->window = SDL_CreateWindow("Screencast", 800, 600, SDL_WINDOW_RESIZABLE);

        if (!data->window) {
            g_printerr("Failed to create window: %s\n", SDL_GetError());
            g_mutex_unlock(&data->frame_mutex);
            g_main_loop_quit(data->main_loop);
            return G_SOURCE_REMOVE;
        }

        data->renderer = SDL_CreateRenderer(data->window, NULL);
        if (!data->renderer) {
            g_printerr("Failed to create renderer: %s\n", SDL_GetError());
            g_mutex_unlock(&data->frame_mutex);
            g_main_loop_quit(data->main_loop);
            return G_SOURCE_REMOVE;
        }

        SDL_SetRenderDrawColor(data->renderer, 0, 0, 0, 255);

        data->texture = SDL_CreateTexture(data->renderer,
                                         SDL_PIXELFORMAT_BGRA32,
                                         SDL_TEXTUREACCESS_STREAMING,
                                         data->width, data->height);
        if (!data->texture) {
            g_printerr("Failed to create texture: %s\n", SDL_GetError());
            g_mutex_unlock(&data->frame_mutex);
            g_main_loop_quit(data->main_loop);
            return G_SOURCE_REMOVE;
        }

        data->window_created = TRUE;
        data->needs_texture_recreate = FALSE;
        g_print("Window created: %dx%d\n", data->width, data->height);
    }

    if (data->needs_texture_recreate && data->window_created) {
        if (data->texture) {
            SDL_DestroyTexture(data->texture);
        }

        data->texture = SDL_CreateTexture(data->renderer,
                                         SDL_PIXELFORMAT_BGRA32,
                                         SDL_TEXTUREACCESS_STREAMING,
                                         data->width, data->height);
        if (!data->texture) {
            g_printerr("Failed to recreate texture: %s\n", SDL_GetError());
            g_mutex_unlock(&data->frame_mutex);
            g_main_loop_quit(data->main_loop);
            return G_SOURCE_REMOVE;
        }

        data->needs_texture_recreate = FALSE;
        g_print("Texture recreated: %dx%d\n", data->width, data->height);
    }

    if (data->new_frame && data->texture && data->renderer) {
        SDL_UpdateTexture(data->texture, NULL, data->frame_buffer, data->width * 4);

        int window_w, window_h;
        SDL_GetWindowSize(data->window, &window_w, &window_h);

        float stream_aspect = (float)data->width / (float)data->height;
        float window_aspect = (float)window_w / (float)window_h;

        SDL_FRect dst_rect;

        if (stream_aspect > window_aspect) {
            dst_rect.w = window_w;
            dst_rect.h = window_w / stream_aspect;
            dst_rect.x = 0;
            dst_rect.y = (window_h - dst_rect.h) / 2;
        } else {
            dst_rect.h = window_h;
            dst_rect.w = window_h * stream_aspect;
            dst_rect.x = (window_w - dst_rect.w) / 2;
            dst_rect.y = 0;
        }

        SDL_RenderClear(data->renderer);
        SDL_RenderTexture(data->renderer, data->texture, NULL, &dst_rect);
        SDL_RenderPresent(data->renderer);
        data->new_frame = FALSE;
    }

    g_mutex_unlock(&data->frame_mutex);

    return G_SOURCE_CONTINUE;
}

int main(int argc, char *argv[]) {
    XdpPortal *portal;
    AppData data = {0};

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    global_data = &data;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        g_printerr("Failed to initialize SDL: %s\n", SDL_GetError());
        return 1;
    }

    g_mutex_init(&data.frame_mutex);

    portal = xdp_portal_new();
    data.main_loop = g_main_loop_new(NULL, FALSE);

    xdp_portal_create_screencast_session(portal,
                                        XDP_OUTPUT_MONITOR | XDP_OUTPUT_WINDOW | XDP_OUTPUT_VIRTUAL,
                                        XDP_SCREENCAST_FLAG_NONE,
                                        XDP_CURSOR_MODE_HIDDEN,
                                        XDP_PERSIST_MODE_TRANSIENT,
                                        NULL,
                                        NULL,
                                        on_screencast_created,
                                        &data);

    g_timeout_add(16, check_sdl_events, &data);

    g_main_loop_run(data.main_loop);

    if (data.loop) {
        pw_thread_loop_lock(data.loop);
        if (data.stream) {
            pw_stream_destroy(data.stream);
            data.stream = NULL;
        }
        pw_thread_loop_unlock(data.loop);
        pw_thread_loop_stop(data.loop);
        pw_thread_loop_destroy(data.loop);
    }

    if (data.texture)
        SDL_DestroyTexture(data.texture);
    if (data.renderer)
        SDL_DestroyRenderer(data.renderer);
    if (data.window)
        SDL_DestroyWindow(data.window);

    g_mutex_lock(&data.frame_mutex);
    if (data.frame_buffer)
        g_free(data.frame_buffer);
    g_mutex_unlock(&data.frame_mutex);

    g_mutex_clear(&data.frame_mutex);
    g_main_loop_unref(data.main_loop);
    g_object_unref(portal);
    SDL_Quit();
    pw_deinit();

    return 0;
}
