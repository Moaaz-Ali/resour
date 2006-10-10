#!/bin/bash

#
#  Copyright Red Hat, Inc. 2006
#
#  This program is free software; you can redistribute it and/or modify it
#  under the terms of the GNU General Public License as published by the
#  Free Software Foundation; either version 2, or (at your option) any
#  later version.
#
#  This program is distributed in the hope that it will be useful, but
#  WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
#  General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; see the file COPYING.  If not, write to the
#  Free Software Foundation, Inc.,  675 Mass Ave, Cambridge, 
#  MA 02139, USA.
#

declare RA_COMMON_pid_dir=/var/run/cluster
declare RA_COMMON_conf_dir=/etc/cluster

declare -i FAIL=-1
declare -a ip_keys

generate_configTemplate()
{
	cat > $1 << EOT
#
# "$1" was created from the "$2"
#
# This template configuration was automatically generated, and will be
# automatically regenerated if removed. Once this file has been altered,
# automatic re-generation will stop. Remember to copy this file to all 
# other cluster members after making changes, or your service will not 
# operate correctly.
#
EOT
}

sha1_addToFile()
{
        declare sha1line="# rgmanager-sha1 $(sha1sum "$1")"
        echo $sha1line >> "$1"
}

sha1_verify()
{
	declare sha1_new sha1_old
	declare oldFile=$1

	ocf_log debug "Checking: SHA1 checksum of config file $oldFile"

	sha1_new=`cat $oldFile | grep -v "# rgmanager-sha1" | sha1sum | sed 's/^\([a-z0-9]\+\) .*$/\1/'`
	sha1_old=`tail -n 1 $oldFile | sed 's/^# rgmanager-sha1 \(.*\)$/\1/' | sed 's/^\([a-z0-9]\+\) .*$/\1/'`

	if [ $sha1_new = $sha1_old ]; then
	        ocf_log debug "Checking: SHA1 checksum > succeed"
		return 0;
	else
		ocf_log debug "Checking: SHA1 checksum > failed - file changed"
		return 1;
	fi
}

#
# Usage: ccs_connect
# Returns: $FAIL on failure, or a connection descriptor on success
#
ccs_connect()
{
	declare outp

	outp=$(ccs_test connect 2>&1)
	if [ $? -ne 0 ]; then
		ocf_log err "$outp"
		return $FAIL
	fi

	outp=${outp/*= /}
	if [ -n "$outp" ]; then
		echo $outp
		return 0
	fi

	return 1
}

#
# Usage: ccs_disconnect descriptor
#
ccs_disconnect()
{
	declare outp

	[ -n "$1" ] || return $FAIL
	outp=$(ccs_test disconnect $1 2>&1)
	if [ $? -ne 0 ]; then
		ocf_log warn "Disconnect CCS desc $1 failed: $outp"
		return 1
	fi
	return 0
}

#
# Usage: ccs_get desc key
#
ccs_get()
{
	declare outp
	declare ccsfd=$1
	declare key

	[ -n "$1" ] || return $FAIL
	[ -n "$2" ] || return $FAIL

	shift
	key="$*"

	outp=$(ccs_test get $ccsfd "$key" 2>&1)
	if [ $? -ne 0 ]; then
		if [ "$outp" = "${outp/No data available/}" ]; then
			ocf_log err "$outp ($key)"
			return $FAIL
		fi

		# no real error, just no data available
		return 0
	fi

	outp=${outp/*</}
	outp=${outp/>*/}

	echo $outp

	return 0
}

#
# Build a list of service IP keys; traverse refs if necessary
# Usage: get_service_ip_keys desc serviceName
#
get_service_ip_keys()
{
	declare ccsfd=$1
	declare svc=$2
	declare -i x y=0
	declare outp
	declare key

	if [ $ccsfd -eq $FAIL ]; then
		ocf_log err "Can not talk to ccsd: invalid descriptor $ccsfd"
		return 1
	fi

	#
	# Find service-local IP keys
	#
	x=1
	while : ; do
		key="/cluster/rm/service[@name=\"$svc\"]/ip[$x]"

		#
		# Try direct method
		#
		outp=$(ccs_get $ccsfd "$key/@address")
		if [ $? -ne 0 ]; then
			return 1
		fi

		#
		# Try by reference
		#
		if [ -z "$outp" ]; then
			outp=$(ccs_get $ccsfd "$key/@ref")
			if [ $? -ne 0 ]; then
				return 1
			fi
			key="/cluster/rm/resources/ip[@address=\"$outp\"]"
		fi

		if [ -z "$outp" ]; then
			break
		fi

		#ocf_log debug "IP $outp found @ $key"

		ip_keys[$y]="$key"

		((y++))
		((x++))
	done

	ocf_log debug "$y IP addresses found for $svc/$OCF_RESKEY_name"

	return 0
}

build_ip_list()
{
        declare -i ccsfd=$1
        declare ipaddrs ipaddr
        declare -i x=0
                        
        while [ -n "${ip_keys[$x]}" ]; do
              ipaddr=$(ccs_get $ccsfd "${ip_keys[$x]}/@address")
              if [ -z "$ipaddr" ]; then
                                   break
              fi

              ipaddrs="$ipaddrs $ipaddr"
             ((x++))
        done

        echo $ipaddrs
}

generate_name_for_pid_file()
{
	declare filename=$(basename $0)
	
	echo "$RA_COMMON_pid_dir/$(basename $0 | sed 's/^\(.*\)\..*/\1/')/$OCF_RESOURCE_INSTANCE.pid"
	
	return 0;
}

generate_name_for_pid_dir()
{
	declare filename=$(basename $0)
	
	echo "$RA_COMMON_pid_dir/$(basename $0 | sed 's/^\(.*\)\..*/\1/')/$OCF_RESOURCE_INSTANCE"
	
	return 0;
}

generate_name_for_conf_dir()
{
	declare filename=$(basename $0)

	echo "$RA_COMMON_conf_dir/$(basename $0 | sed 's/^\(.*\)\..*/\1/')/$OCF_RESOURCE_INSTANCE"
	
	return 0;
}

create_pid_directory()
{
	declare program_name="$(basename $0 | sed 's/^\(.*\)\..*/\1/')"
	declare dirname="$RA_COMMON_pid_dir/$program_name"

	if [ -d "$dirname" ]; then
		return 0;
	fi
	
	chmod 711 "$RA_COMMON_pid_dir"
	mkdir -p "$dirname"
	
	if [ "$program_name" = "mysql" ]; then
		chown mysql.root "$dirname"
	elif [ "$program_name" = "tomcat-5" ]; then
		chown tomcat.root "$dirname"
	fi

	return 0;
}

create_conf_directory()
{
	declare dirname="$1"

	if [ -d "$dirname" ]; then
		return 0;
	fi
	
	mkdir -p "$dirname"
	
	return 0;
}

check_pid_file() {
	declare pid_file="$1"

	if [ -z "$pid_file" ]; then
		return 1;
	fi

	if [ ! -e "$pid_file" ]; then
		return 0;
	fi

	if [ ! -d /proc/`cat "$pid_file"` ]; then	
		rm "$pid_file"
		ocf_log debug "PID File \"$pid_file\" Was Removed - PID Does Not Exist";
		return 0;
	fi

	return 1;
}