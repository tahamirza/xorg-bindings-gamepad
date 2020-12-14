#!/bin/awk -f

BEGIN { print "struct key_binding_t bindings[] = {"; ind = 0 }

/EV_KEY/ { next }
/EV_ABS/ { next }
/#/ { $1 = ""; print "//" $0; next }

/class/ { class = $2 }

/ctrl/ {
    if (ctrl)
	ictrl++
    else
	ictrl = 0
    ctrl = $2;
    print "// " $0, ictrl
}

/delay/ { delay = $2 }
/repeat/ { repeat = $2 }

/BTN/ {
    i = ind++
    ctrls[ictrl]["EV_KEY"][$1][0] = i
    print "// index " i
    print "// " $0
    print "{"
    print "\t.press_threshold = 1,"
    print "\t.release_threshold = 0,"
    print "\t.repeat_ms = " repeat ","
    print "\t.first_repeat_delay_ms = " delay ","
    print "\t.window_class = \"" class "\","
    print "\t.keycode = " $2 ","
    if ($3)
	print "\t.keystate = " $3 ","
    print "},"
}

/deadzone/ { deadzone = $2 }
/hold/ { hold = $2 }

/ABS/ {
    i = ind++
    ctrls[ictrl]["EV_ABS"][$1][$2] = i
    print "// index " i
    print "// " $0
    print "{"
    print "\t.press_threshold = " $2 deadzone ","
    print "\t.release_threshold = " $2 deadzone ","
    print "\t.repeat_ms = " repeat ","
    print "\t.first_repeat_delay_ms = " delay ","
    print "\t.window_class = \"" class "\","
    print "\t.keycode = " $3 ","
    if ($4)
	print "\t.keystate = " $4 ","
    if (hold)
	print "\t.hold_threshold_ms = " hold ","
    print "},"

}

END {
    print "};"

    print "static int find_matching_bindings(int32_t ctrl, uint16_t type, uint16_t code, int32_t ret[2])"
    print "{"
    print "\tswitch (ctrl)"
    print "\t{"
    for (c in ctrls) {
	print "\tcase " c ":"
	print "\t\tswitch (type)"
	print "\t\t{"
	for (t in ctrls[c]) {
	    print "\t\tcase " t ":"
	    print "\t\t\tswitch (code)"
	    print "\t\t\t{"
	    for (k in ctrls[c][t]) {
		print "\t\t\tcase " k ":"
		i = 0
		for (b in ctrls[c][t][k]) {
		    print "\t\t\t\tret[" i++ "] = " ctrls[c][t][k][b] ";"
		}
		print "\t\t\t\treturn " i ";"
		print "\t\t\tbreak;"
	    }
	    print "\t\t\tdefault:"
	    print "\t\t\tbreak;"
	    print "\t\t\t}"
	    print "\t\tbreak;"
	}
        print "\t\tdefault:"
	print "\t\tbreak;"
	print "\t\t}"
	print "\tbreak;"
    }
    print "\tdefault:"
    print "\tbreak;"
    print "\t}"

    print "return 0;"
    print "}"
}
