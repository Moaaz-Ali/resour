#!/usr/bin/python

##
## Copyright (C) 2008 Red Hat, Inc. All Rights Reserved.
##
## The Following Agent Has Been Tested On:
##
##  iLO Version       
## +---------------------------------------------+
##  iLO Advanced 1.91 
##
## @note: We can't use conn.sendline because we need to send CR/LF
#####

import sys, re, pexpect
sys.path.append("../lib/")
from fencing import *

def get_power_status(conn, options):
	try:
		conn.send("POWER\r\n")
		conn.log_expect(options, options["-c"], SHELL_TIMEOUT)
	except pexpect.EOF:
		fail(EC_CONNECTION_LOST)
	except pexpect.TIMEOUT:
		fail(EC_TIMED_OUT)
				
	status = re.compile("server power is currently: (.*)", re.IGNORECASE).search(conn.before).group(1)
	return status.lower().strip()

def set_power_status(conn, options):
	action = {
		'on' : "powerup",
		'off': "powerdown"
	}[options["-o"]]

	try:
		conn.send("power " + options["-o"] + "\r\n")
		conn.log_expect(options, options["-c"], POWER_TIMEOUT)
	except pexcept.EOF:
		fail(EC_CONNECTION_LOST)
	except pexcept.TIMEOUT:
		fail(EC_TIMED_OUT)

def main():
	device_opt = [  "help", "version", "agent", "quiet", "verbose", "debug",
			"action", "ipaddr", "login", "passwd", "passwd_script",
			"secure" ]

	options = check_input(device_opt, process_input(device_opt))

	## 
	## Fence agent specific defaults
	#####
	if 0 == options.has_key("-c"):
		options["-c"] = "</>hpiLO->"

	##
	## Operate the fencing device
	####
	conn = fence_login(options)
	fence_action(conn, options, set_power_status, get_power_status)

	##
	## Logout from system
	######
	conn.send("quit\r\n")
	conn.close()

if __name__ == "__main__":
	main()
