#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xcb/xcb.h>

void send_event_to_window_deep(xcb_connection_t* c, xcb_window_t win, int depth)
{
    xcb_query_tree_cookie_t query_cookie;
    xcb_query_tree_reply_t* query_reply = NULL;
    xcb_get_property_cookie_t prop_cookie;
    xcb_get_property_reply_t *prop_reply = NULL;
    xcb_atom_t property = XCB_ATOM_WM_CLASS;
    xcb_atom_t type = XCB_ATOM_STRING;
    int i;

    prop_cookie = xcb_get_property(c, 0, win, property, type, 0, 8);
    query_cookie = xcb_query_tree(c, win);

    if ((prop_reply = xcb_get_property_reply(c, prop_cookie, NULL)))
    {
	int len = xcb_get_property_value_length(prop_reply);
	if (len > 0)
	{
	    const char* class = "gwenview";
	    const int class_len = strlen(class);
	    char* prop_string = (char*)xcb_get_property_value(prop_reply);

	    if (len >= class_len && memcmp(class, prop_string, class_len) == 0)
	    {
		printf("%d class matches!\n", win);

		xcb_key_press_event_t event = { 0 };
		event.response_type = XCB_KEY_PRESS;
		event.detail = 113;
		event.root = win;
		event.event = win;
		event.state = XCB_NONE;
		event.same_screen = 1;

		xcb_send_event(c, 0, win, XCB_EVENT_MASK_KEY_PRESS, (char*)&event);
	    }
	}
	free(prop_reply);
    }

    query_reply = xcb_query_tree_reply(c, query_cookie, NULL);
    if (query_reply)
    {
	xcb_window_t* children = xcb_query_tree_children(query_reply);
	for (i = 0; i < xcb_query_tree_children_length(query_reply); i++)
	{
	    send_event_to_window_deep(c, children[i], depth + 1);
	}
	free(query_reply);
    }
}

int main(int argc, char **argv)
{
    xcb_connection_t *c = NULL;
    xcb_screen_t *screen = NULL;
    int screen_num = 0;
    const xcb_setup_t *setup;
    xcb_screen_iterator_t iter;
    int i;

    c = xcb_connect(NULL, &screen_num);

    if (c == NULL)
    {
	fprintf(stderr, "Unable to open XCB display.\n");
	exit(1);
    }

    setup = xcb_get_setup(c);

    iter = xcb_setup_roots_iterator(setup);

    for (i = 0; i < screen_num; i++)
    {
	xcb_screen_next(&iter);
    }

    screen = iter.data;

    send_event_to_window_deep(c, screen->root, 0);

    xcb_disconnect(c);
    return 0;
}
