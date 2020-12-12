#include <stdio.h>
#include <stdlib.h>
#include <xcb/xcb.h>

int main(int argc, char **argv)
{
    xcb_connection_t *c = NULL;
    xcb_screen_t *screen = NULL;
    int screen_num = 0;
    const xcb_setup_t *setup;
    xcb_screen_iterator_t iter;
    int i;
    xcb_query_tree_cookie_t query_cookie;
    xcb_query_tree_reply_t* query_reply = NULL;

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

    query_cookie = xcb_query_tree(c, screen->root);
    query_reply = xcb_query_tree_reply(c, query_cookie, NULL);

    if (query_reply)
    {
	printf("Got query reply.\n");
	xcb_window_t* children = xcb_query_tree_children(query_reply);
	for (i = 0; i < xcb_query_tree_children_length(query_reply); i++)
	{
	    xcb_get_property_cookie_t prop_cookie;
	    xcb_get_property_reply_t *prop_reply;
	    xcb_atom_t property = XCB_ATOM_WM_CLASS;
	    xcb_atom_t type = XCB_ATOM_STRING;
	    prop_cookie = xcb_get_property(c, 0, children[i], property, type, 0, 8);
	    if ((prop_reply = xcb_get_property_reply(c, prop_cookie, NULL)))
	    {
		int len = xcb_get_property_value_length(prop_reply);
		if (len > 0)
		{
		    printf("WM_CLASS is %.*s\n", len, (char*)xcb_get_property_value(prop_reply));
		}
		free(prop_reply);
	    }
	}
	free(query_reply);
    }

    xcb_disconnect(c);
    return 0;
}
