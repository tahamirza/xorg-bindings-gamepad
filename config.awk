#!/bin/awk -f

BEGIN { print "struct key_binding_t bindings[] = {"; ind = 0 }

/^#/ { $1 = ""; print "//" $0; next }

/^class/ { class = $2 }

/^ctrl/ {
    if (ctrl)
	ictrl++
    else
	ictrl = 0
    ctrl = $2
    set = 0
    print "// " $0, ictrl
}

/^delay/ { delay = $2 }
/^repeat/ { repeat = $2 }
/^rumble/ { rumble = $2 }
/^set/ { set = $2 }

/^BTN/ {
    i = ind++
    ctrls[ictrl, "EV_KEY", $1, set] = i
    print "// index " i
    print "// " $0
    print "{"
    print "\t.press_threshold = 1,"
    print "\t.release_threshold = 0,"
    print "\t.repeat_ms = " repeat ","
    print "\t.first_repeat_delay_ms = " delay ","
    print "\t.window_class = \"" class "\","
    print "\t.set = " set ","
    if ($2 == "setnext") {
	print "\t.setnext = true,"
    } else if ($2 == "setprev") {
        print "\t.setprev = true,"
    } else {
        print "\t.keycode = " $2 ","
    }
    if ($3)
	print "\t.keystate = " $3 ","
    print "},"
}

/^deadzone/ { deadzone = $2 }
/^hold/ { hold = $2 }

/^ABS/ {
    i = ind++
    ctrls[ictrl, "EV_ABS", $1, $2] = i
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
    if (rumble == "on")
	print "\t.rumble = true,"
    print "},"

}

END {
    print "};"

    print "static int find_matching_bindings(int32_t ctrl, uint16_t type, uint16_t code, int32_t *ret, int32_t retsize)"
    print "{"
    print "\tint num = 0;"
    print ""
    for (i in ctrls) {
	split(i, c, SUBSEP)
	print "\tif ( ctrl == " c[1] " && type == " c[2] " && code == " c[3] " ) { ret[num++] = " ctrls[i] "; if (num >= retsize) return num; }"
    }
    print ""
    print "\treturn num;"
    print "}"
}
