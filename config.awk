#!/bin/awk -f

BEGIN { print "struct key_binding_t bindings[] = {" }

/EV_KEY/ { next }
/EV_ABS/ { next }
/#/ { $1 = ""; print "//" $0; next }

/class/ { class = $2 }

/ctrl/ {
    if (ctrl)
	print "}, // END " ctrl

    ctrl = $2;
    ibtn = 0;
    iabs = 0;
    print "// " $0
    print "{"
}

/BTN/ {
    # if (ibtn == 0)
    # 	print "{ // btn_index"
    print "// index " ibtn++
    print "// " $0
    print "{"
    print "\t.press_threshold = 1,"
    print "\t.release_threshold = 0,"
    print "\t.window_class = \"" class "\","
    print "\t.keycode = " $2 ","
    if ($3)
	print "\t.keystate = " $3 ","
    print "},"
}

/deadzone/ { deadzone = $2 }
/hold/ { hold = $2 }

/ABS/ {
    if (ibtn) {
	# print "} // END btn_index"
	iabs = ibtn
	ibtn = 0
    }
    print "// index " iabs++
    print "// " $0
    print "{"
    print "\t.press_threshold = " $2 deadzone ","
    print "\t.release_threshold = " $2 deadzone ","
    print "\t.window_class = \"" class "\","
    print "\t.keycode = " $3 ","
    if ($4)
	print "\t.keystate = " $4 ","
    if (hold)
	print "\t.hold_threshold_ms = " hold ","
    print "},"

}

#{ print $0 }

END {
    if (ctrl)
	print "}, // END " ctrl
    print "}"
}
