#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <xcb/xcb.h>
#include <libudev.h>
#include <errno.h>
#include <linux/input.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/epoll.h>

/* stolen from bsd's time.h */
/* Operations on timespecs */
#define	timespecclear(tvp)	((tvp)->tv_sec = (tvp)->tv_nsec = 0)
#define	timespecisset(tvp)	((tvp)->tv_sec || (tvp)->tv_nsec)
#define	timespeccmp(tvp, uvp, cmp)					\
	(((tvp)->tv_sec == (uvp)->tv_sec) ?				\
	    ((tvp)->tv_nsec cmp (uvp)->tv_nsec) :			\
	    ((tvp)->tv_sec cmp (uvp)->tv_sec))

#define	timespecadd(tsp, usp, vsp)					\
	do {								\
		(vsp)->tv_sec = (tsp)->tv_sec + (usp)->tv_sec;		\
		(vsp)->tv_nsec = (tsp)->tv_nsec + (usp)->tv_nsec;	\
		if ((vsp)->tv_nsec >= 1000000000L) {			\
			(vsp)->tv_sec++;				\
			(vsp)->tv_nsec -= 1000000000L;			\
		}							\
	} while (0)
#define	timespecsub(tsp, usp, vsp)					\
	do {								\
		(vsp)->tv_sec = (tsp)->tv_sec - (usp)->tv_sec;		\
		(vsp)->tv_nsec = (tsp)->tv_nsec - (usp)->tv_nsec;	\
		if ((vsp)->tv_nsec < 0) {				\
			(vsp)->tv_sec--;				\
			(vsp)->tv_nsec += 1000000000L;			\
		}							\
	} while (0)

/* Operations on timevals. */

#define	timevalclear(tvp)		((tvp)->tv_sec = (tvp)->tv_usec = 0)
#define	timevalisset(tvp)		((tvp)->tv_sec || (tvp)->tv_usec)
#define	timevalcmp(tvp, uvp, cmp)					\
	(((tvp)->tv_sec == (uvp)->tv_sec) ?				\
	    ((tvp)->tv_usec cmp (uvp)->tv_usec) :			\
	    ((tvp)->tv_sec cmp (uvp)->tv_sec))


xcb_connection_t *c = NULL;
xcb_screen_t *screen = NULL;
const char* role_atom_name = "WM_WINDOW_ROLE";
xcb_atom_t role_atom;

struct key_binding_t
{
    int32_t press_threshold;
    int32_t release_threshold;
    struct timespec first_press;
    struct timespec last_release;
    struct timespec last_activation;
    uint32_t hold_threshold_ms;
    uint32_t repeat_ms;
    const char* window_class;
    xcb_keycode_t keycode;
    uint16_t keystate;
};

#include "config.gen.inc.c"

static void send_event_to_window_deep(xcb_window_t win, const char* class, xcb_keycode_t keycode, uint16_t keystate)
{
    const int class_len = strlen(class);
    const char* role = "MainWindow";
    const int role_len = strlen(role);

    xcb_query_tree_cookie_t query_cookie;
    xcb_query_tree_reply_t* query_reply = NULL;
    xcb_get_property_cookie_t class_cookie;
    xcb_get_property_reply_t *class_reply = NULL;
    xcb_get_property_cookie_t role_cookie;
    xcb_get_property_reply_t *role_reply = NULL;

    bool has_role = false;
    bool has_class = false;
    int i;

    class_cookie = xcb_get_property(c, 0, win, XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 0, 8);
    role_cookie = xcb_get_property(c, 0, win, role_atom, XCB_ATOM_STRING, 0, 8);
    query_cookie = xcb_query_tree(c, win);

    if ((class_reply = xcb_get_property_reply(c, class_cookie, NULL)))
    {
	has_class = (xcb_get_property_value_length(class_reply) >= class_len
		     && memcmp(class, xcb_get_property_value(class_reply), class_len) == 0);
	free(class_reply);
    }

    if ((role_reply = xcb_get_property_reply(c, role_cookie, NULL)))
    {
	has_role = (xcb_get_property_value_length(role_reply) >= role_len
		    && memcmp(role, xcb_get_property_value(role_reply), role_len) == 0);
	free(role_reply);
    }

    if (has_class && has_role)
    {
	printf("sending press event to %d!\n", win);

	xcb_key_press_event_t event = { 0 };
	event.response_type = XCB_KEY_PRESS;
	event.detail = keycode;
	event.root = win;
	event.event = win;
	event.state = keystate;
	event.same_screen = 1;

	xcb_send_event(c, 0, win, XCB_EVENT_MASK_KEY_PRESS, (char*)&event);

	event.response_type = XCB_KEY_RELEASE;
	xcb_send_event(c, 0, win, XCB_EVENT_MASK_KEY_RELEASE, (char*)&event);
    }

    if ((query_reply = xcb_query_tree_reply(c, query_cookie, NULL)))
    {
	xcb_window_t* children = xcb_query_tree_children(query_reply);
	for (i = 0; i < xcb_query_tree_children_length(query_reply); i++)
	{
	    send_event_to_window_deep(children[i], class, keycode, keystate);
	}
	free(query_reply);
    }
}

static void process_binding_event(struct input_event* event, int32_t index)
{
    struct timespec curr_time;
    clock_gettime(CLOCK_MONOTONIC, &curr_time);
    struct key_binding_t *binding = &bindings[index];

    if (event->value > 0 && binding->press_threshold > 0)
    {
	if (event->value >= binding->press_threshold)
	{
	    if (timespeccmp(&binding->first_press, &binding->last_release, <=))
	    {
		printf("Button pressed! %d %d\n", binding->press_threshold, event->value);
		binding->first_press = curr_time;
	    }
	    return;
	}
    }
    
    if (event->value < 0 && binding->press_threshold < 0)
    {
	if (event->value <= binding->press_threshold)
	{
	    if (timespeccmp(&binding->first_press, &binding->last_release, <=))
	    {
		printf("Button pressed! %d %d\n", binding->press_threshold, event->value);
		binding->first_press = curr_time;
	    }
	    return;
	}
    }
    
    printf ("Button released! %d %d\n", binding->press_threshold, event->value);
    binding->last_release = curr_time;
}

static bool is_binding_pending(struct key_binding_t* binding)
{
    struct timespec curr_time;
    struct timespec activation_diff;
    clock_gettime(CLOCK_MONOTONIC, &curr_time);

    timespecsub(&curr_time, &binding->last_activation, &activation_diff);

    if (timespeccmp(&binding->first_press, &binding->last_release, >)
	&& timespeccmp(&binding->first_press, &binding->last_activation, >))
    {
	printf("There is a binding pending!\n");
	return true;
    }

    return false;
}

static void fire_binding(struct key_binding_t* binding)
{
    struct timespec curr_time;
    clock_gettime(CLOCK_MONOTONIC, &curr_time);

    printf("Firing binding!\n");
    binding->last_activation = curr_time;

    send_event_to_window_deep(screen->root, binding->window_class, binding->keycode, binding->keystate);
}

static void fire_pending_bindings()
{
    int num_bindings = sizeof bindings / sizeof bindings[0];
    int i;

    printf("Number of bindings: %d\n", num_bindings);

    for (i = 0; i < num_bindings; i++)
    {
	if (is_binding_pending(&bindings[i]))
	{
	    fire_binding(&bindings[i]);
	}
    }
}

int main(int argc, char **argv)
{
    int screen_num = 0;
    const xcb_setup_t *setup;
    xcb_screen_iterator_t iter;
    xcb_intern_atom_cookie_t atom_cookie;
    xcb_intern_atom_reply_t* atom_reply;

    struct udev* udev = NULL;
    struct udev_device* device = NULL;
    const char* device_name;
    const char* device_path;
    struct input_event event;
    int dev_fd = -1;
    int epoll_fd = -1;

    int i = 0;
    ssize_t read_result = 0;
    int epoll_result = 0;

    if (argc != 2)
    {
	fprintf(stderr, "Invalid number of arguments. Need exactly one.\n");
	exit(1);
    }

    printf("%s\n", argv[1]);

    udev = udev_new();

    if (!udev)
    {
	fprintf(stderr, "Unable to create udev context.\n");
	exit(1);
    }

    device = udev_device_new_from_syspath(udev, argv[1]);

    if (!device)
    {
	fprintf(stderr, "Unable to open device from syspath: %d.\n", errno);
	exit(1);
    }

    device_name = udev_device_get_property_value(udev_device_get_parent(device), "NAME");
    device_path = udev_device_get_devnode(device);

    if (device_name && device_path)
    {
	printf("Starting up to monitor %s on %s\n", device_name, device_path);
    }
    else
    {
	fprintf(stderr, "Could not get name of device.\n");
	exit(1);
    }

    if ((dev_fd = open(device_path, O_RDONLY)) == -1)
    {
	fprintf(stderr, "Could not open device node: %d\n", errno);
	exit(1);
    }

    if ((epoll_fd = epoll_create1(0)) == -1)
    {
	fprintf(stderr, "Could not initialize epoll context: %d\n", errno);
	exit(1);
    }

    struct epoll_event epoll_event = { 0 };
    epoll_event.events = EPOLLIN;
    epoll_event.data.fd = dev_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, dev_fd, &epoll_event) == -1)
    {
	fprintf(stderr, "Could not add device fd to epoll context: %d\n", errno);
	exit(1);
    }

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
    atom_cookie = xcb_intern_atom(c, 0, strlen(role_atom_name), role_atom_name);
    if ((atom_reply = xcb_intern_atom_reply(c, atom_cookie, NULL)))
    {
	role_atom = atom_reply->atom;
	free(atom_reply);
	
    }
    else
    {
	fprintf(stderr, "Could not intern window role atom.\n");
	exit (1);
    }

    for(;;)
    {
	int32_t binding_indices[2];
	int bindings_found;

	fire_pending_bindings();

	epoll_result = epoll_wait(epoll_fd, &epoll_event, 1, 100);

	if (epoll_result == -1)
	{
	    fprintf(stderr, "Error waiting with epoll. %d\n", errno);
	    break;
	}

	if (epoll_result == 0)
	{
	    // epoll timed out
	    continue;
	}

	if (!(epoll_event.events & EPOLLIN))
	{
	    fprintf(stderr, "Device fd is not readable. \n");
	    break;
	}

	read_result = read(dev_fd, &event, sizeof event);

	if (read_result == -1)
	{
	    fprintf(stderr, "Error reading from device. %d\n", errno);
	    break;
	}

	bindings_found = find_matching_bindings(0, event.type, event.code, binding_indices);

	for (i = 0; i < bindings_found; i++)
	{
	    process_binding_event(&event, binding_indices[i]);
	}
    }


    xcb_disconnect(c);
    close(epoll_fd);
    close(dev_fd);
    udev_device_unref(device);
    udev_unref(udev);
    return 0;
}
