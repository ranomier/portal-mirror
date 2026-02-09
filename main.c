#include <libportal/portal.h>
#include <glib.h>

static void
on_screencast_created (GObject *source,
                       GAsyncResult *result,
                       gpointer user_data)
{
  XdpPortal *portal = XDP_PORTAL (source);
  GMainLoop *loop = user_data;
  g_autoptr(GError) error = NULL;
  XdpSession *session;

  session = xdp_portal_create_screencast_session_finish (portal, result, &error);
  
  if (error)
    {
      g_printerr ("Error: %s\n", error->message);
      g_main_loop_quit (loop);
      return;
    }

  g_print ("Screencast session created\n");
  g_object_unref (session);
  g_main_loop_quit (loop);
}

int
main (int argc, char *argv[])
{
  XdpPortal *portal;
  GMainLoop *loop;

  portal = xdp_portal_new ();
  loop = g_main_loop_new (NULL, FALSE);

  xdp_portal_create_screencast_session (portal,
                                        XDP_OUTPUT_MONITOR | XDP_OUTPUT_WINDOW | XDP_OUTPUT_VIRTUAL,
                                        XDP_SCREENCAST_FLAG_NONE,
                                        XDP_CURSOR_MODE_HIDDEN,
                                        XDP_PERSIST_MODE_TRANSIENT,
                                        NULL,
                                        NULL,
                                        on_screencast_created,
                                        loop);

  g_main_loop_run (loop);

  g_main_loop_unref (loop);
  g_object_unref (portal);

  return 0;
}
