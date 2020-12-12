#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xcb/xcb.h>

xcb_atom_t role_atom;

void send_event_to_window_deep(xcb_connection_t* c, xcb_window_t win, int depth)
{
    xcb_query_tree_cookie_t query_cookie;
    xcb_query_tree_reply_t* query_reply = NULL;
    xcb_get_property_cookie_t class_cookie;
    xcb_get_property_reply_t *class_reply = NULL;
    xcb_get_property_cookie_t role_cookie;
    xcb_get_property_reply_t *role_reply = NULL;
    int i;

    class_cookie = xcb_get_property(c, 0, win, XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 0, 8);
    role_cookie = xcb_get_property(c, 0, win, role_atom, XCB_ATOM_STRING, 0, 8);
    query_cookie = xcb_query_tree(c, win);

    if ((class_reply = xcb_get_property_reply(c, class_cookie, NULL)))
    {
	const char* class = "gwenview";
	const int class_len = strlen(class);
	if (xcb_get_property_value_length(class_reply) >= class_len
	    && memcmp(class, xcb_get_property_value(class_reply), class_len) == 0)
	{
	    if ((role_reply = xcb_get_property_reply(c, role_cookie, NULL)))
	    {
		const char* role = "MainWindow";
		const int role_len = strlen(role);

		if (xcb_get_property_value_length(role_reply) >= role_len
		    && memcmp(role, xcb_get_property_value(role_reply), role_len) == 0)
		{
		    printf("sending event to %d!\n", win);

		    xcb_key_press_event_t event = { 0 };
		    event.response_type = XCB_KEY_PRESS;
		    event.detail = 113;
		    event.root = win;
		    event.event = win;
		    event.state = XCB_NONE;
		    event.same_screen = 1;

		    xcb_send_event(c, 0, win, XCB_EVENT_MASK_KEY_PRESS, (char*)&event);
		}

		free(role_reply);
	    }
	}
	free(class_reply);
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
    xcb_intern_atom_cookie_t atom_cookie;
    xcb_intern_atom_reply_t* atom_reply;
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
    const char* role_atom_name = "WM_WINDOW_ROLE";
    atom_cookie = xcb_intern_atom(c, 0, strlen(role_atom_name), role_atom_name);
    if ((atom_reply = xcb_intern_atom_reply(c, atom_cookie, NULL)))
    {
	role_atom = atom_reply->atom;
	free(atom_reply);
	send_event_to_window_deep(c, screen->root, 0);
    }
    else
    {
	fprintf(stderr, "Could not intern window role atom.\n");
    }

    xcb_disconnect(c);
    return 0;
}
