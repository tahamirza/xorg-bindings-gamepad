#!/bin/awk -f

BEGIN { }

/EV_KEY/ { next }
/EV_ABS/ { next }
/#/ { next }

{ print $0 }
