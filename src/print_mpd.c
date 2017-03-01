/*
 * vim:ts=4:sw=4:expandtab
 *
 * Implementation with libmpdclient
 *
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yajl/yajl_gen.h>
#include <yajl/yajl_version.h>

#include <mpd/client.h>

#include "i3status.h"

#if 0  // 0 : no debug, 1: debug errors to stderr
#ifndef DEBUG
#define DEBUG
#endif
#endif

/*
 * Macro to generate function (_print_"FUNC_NAME") that will print a
 * music tag ( MPD_TAG_"TAG" )
 *
 */
#define MPDPRINT_TAG(FUNC_NAME, TAG)                                                                                    \
    int _print_##FUNC_NAME(char* buffer, struct mpd_connection* co, struct mpd_status* status, struct mpd_song* song) { \
        int r = sprintf(buffer, "%s", mpd_song_get_tag(song, MPD_TAG_##TAG, 0));                                        \
        if (strcmp(buffer, "(null)") == 0) r = -1;                                                                      \
        return r;                                                                                                       \
    }

/*
 * All functions to print music tags.
 *
 */
MPDPRINT_TAG(title, TITLE)
MPDPRINT_TAG(artist, ARTIST)
MPDPRINT_TAG(album, ALBUM)
MPDPRINT_TAG(date, DATE)
MPDPRINT_TAG(track, TRACK)
MPDPRINT_TAG(albart, ALBUM_ARTIST)
MPDPRINT_TAG(comment, COMMENT)
MPDPRINT_TAG(composer, COMPOSER)
MPDPRINT_TAG(genre, GENRE)

#undef MPDPRINT_TAG

/*
 * Print the path (mpd uri) of the current music played
 *
 */
int _print_path(char* buffer, struct mpd_connection* co, struct mpd_status* status, struct mpd_song* song) {
    return sprintf(buffer, "%s", mpd_song_get_uri(song));
}

/*
 * Association array beetween the format name (name "NAME") and the function
 * to print it (func _print_"NAME")
 *
 */
#define MPDPRINT(NAME) \
    { #NAME, sizeof(#NAME) - 1, _print_##NAME }
struct {
    const char* name;
    int len;
    int (*func)(char* buffer,
                struct mpd_connection* co,
                struct mpd_status* status,
                struct mpd_song* song);
} g_mpd_vars[] = {
    MPDPRINT(path),
    MPDPRINT(title),
    MPDPRINT(artist),
    MPDPRINT(album),
    MPDPRINT(date),
    MPDPRINT(track),
    MPDPRINT(albart),
    MPDPRINT(comment),
    MPDPRINT(composer),
    MPDPRINT(genre)};

#undef MPDPRINT

static int
format_buffer(const char *format, char *buffer, char **outwalk,
              struct mpd_connection* co, struct mpd_status* status, struct mpd_song* song)
{
    const char* f = format;
    char* b = buffer;
    int b_inc, ret = 0;
    while (f[0] != 0) {
        if (f[0] == '%') {
            f++;
            if (f[0] == '%') {
                b[0] = '%';
                b++;
                f++;
            } else {
                for (size_t i = 0; i < sizeof(g_mpd_vars) / sizeof(*g_mpd_vars); i++) {
                    if (strncmp(f, g_mpd_vars[i].name, g_mpd_vars[i].len) == 0) {
                        b_inc = g_mpd_vars[i].func(b, co, status, song);
                        if (b_inc == -1) {
                            ret = -1;
                            goto out;
                        }
                        b += b_inc;
                        f += g_mpd_vars[i].len;
                        break;
                    }
                }
            }
        } else {
            b[0] = f[0];
            b++;
            f++;
        }
    }

out:
    *outwalk = b;
    return ret;
}

/*
 * Main function
 *
 */
void print_mpd(yajl_gen json_gen, char* buffer,
               const char* format, const char* format_off,
               const char* host, int port, const char* password)
{
    char* outwalk = buffer;

    static struct mpd_connection* co = 0;
    struct mpd_status* status = 0;
    struct mpd_song* song = 0;

    if (co != 0 && mpd_connection_get_error(co) != MPD_ERROR_SUCCESS) {
        mpd_connection_free(co);
        co = 0;
    }

    if (co == 0) {
        co = mpd_connection_new(host, port, 0);
        if (co != 0 && password != 0 && *password != 0)  // if password not empty
            mpd_run_password(co, password);
        int err = mpd_connection_get_error(co);
        if (co == 0 || err != MPD_ERROR_SUCCESS) {
            START_COLOR("color_bad");
            outwalk += sprintf(buffer, "%s", format_off);
#ifdef DEBUG
            fprintf(stderr, "mpd: no connection: %s\n", mpd_connection_get_error_message(co));
#endif
            goto end;
        }
    }

    mpd_command_list_begin(co, true);
    mpd_send_status(co);
    mpd_send_current_song(co);
    mpd_command_list_end(co);

    status = mpd_recv_status(co);
    if (!status || mpd_status_get_error(status)) {
        START_COLOR("color_degraded");
        outwalk += sprintf(buffer, "%s", format_off);
#ifdef DEBUG
        fprintf(stderr, "mpd: no status\n");
#endif
        goto end;
    }
    mpd_response_next(co);
    song = mpd_recv_song(co);
    if (!song) {
        START_COLOR("color_degraded");
        outwalk += sprintf(buffer, "%s", format_off);
#ifdef DEBUG
        fprintf(stderr, "mpd: no song\n");
#endif
        goto end;
    }

    if (mpd_status_get_state(status) == MPD_STATE_PLAY)
        START_COLOR("color_good");
    else
        START_COLOR("color_degraded");

    char *orig, *mod;
    orig = mod = strdup(format);
    char *format_try;
    while ((format_try = strsep(&mod, "|")) != NULL) {
        if (format_buffer(format_try, buffer, &outwalk, co, status, song) == 0) {
            break;
        }
    }
    free(orig);

    mpd_response_finish(co);

end:
    if (status)
        mpd_status_free(status);
    if (song)
        mpd_song_free(song);
    if (co) {
        if (mpd_connection_get_error(co) != MPD_ERROR_SUCCESS) {
            mpd_connection_free(co);
            co = 0;
        }
    }

    END_COLOR;
    OUTPUT_FULL_TEXT(buffer);
}
