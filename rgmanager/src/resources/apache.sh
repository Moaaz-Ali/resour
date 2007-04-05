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
#
#  Author(s):
#	Marek Grac (mgrac at redhat.com)
#

export LC_ALL=C
export LANG=C
export PATH=/bin:/sbin:/usr/bin:/usr/sbin

. $(dirname $0)/ocf-shellfuncs
. $(dirname $0)/utils/config-utils.sh
. $(dirname $0)/utils/messages.sh
. $(dirname $0)/utils/ra-skelet.sh

declare APACHE_HTTPD=/usr/sbin/httpd
declare APACHE_serverConfigFile
declare APACHE_pid_file="`generate_name_for_pid_file`"
declare APACHE_conf_dir="`generate_name_for_conf_dir`"
declare APACHE_genConfig="$APACHE_conf_dir/httpd.conf"

declare APACHE_parseConfig=$(dirname $0)/utils/httpd-parse-config.pl

apache_serverConfigFile()
{
	if [[ "$OCF_RESKEY_config_file" =~ '^/' ]]; then
		APACHE_serverConfigFile="$OCF_RESKEY_config_file"
	else 
		APACHE_serverConfigFile="$OCF_RESKEY_server_root/$OCF_RESKEY_config_file"
	fi

	return;
}

verify_all()
{
	clog_service_verify $CLOG_INIT 

	if [ -z "$OCF_RESKEY_name" ]; then
		clog_service_verify $CLOG_FAILED "Invalid Name Of Service"
		return $OCF_ERR_ARGS
	fi

	if [ -z "$OCF_RESKEY_service_name" ]; then
		clog_service_verify $CLOG_FAILED_NOT_CHILD
		return $OCF_ERR_ARGS
	fi
                                                	
	if [ -z "$OCF_RESKEY_server_root" ]; then
		clog_service_verify $CLOG_FAILED "Invalid ServerRoot"
		return $OCF_ERR_ARGS
	fi

	if [ ! -d "$OCF_RESKEY_server_root" ]; then
		clog_service_verify $CLOG_FAILED "ServerRoot Directory Is Missing"
		return $OCF_ERR_ARGS
	fi

	if [ -z "$OCF_RESKEY_config_file" ]; then
		clog_check_file_exist $CLOG_FAILED_INVALID "$OCF_RESKEY_config_file"
		return $OCF_ERR_ARGS
	fi

	if [ ! -r "$APACHE_serverConfigFile" ]; then
		clog_check_file_exist $CLOG_FAILED_NOT_READABLE "$APACHE_config_file"
		return $OCF_ERR_ARGS
	fi

	if [ -z "$APACHE_pid_file" ]; then
		clog_service_verify $CLOG_FAILED "Invalid name of PID file"
		return $OCF_ERR_ARGS
	fi

	clog_check_syntax $CLOG_INIT "$APACHE_serverConfigFile"

	"$APACHE_HTTPD" -t \
		-D"$OCF_RESKEY_name" \
		-d "$OCF_RESKEY_server_root" \
		-f "$APACHE_serverConfigFile" \
		$OCF_RESKEY_httpd_options &> /dev/null
		
	if [ $? -ne 0 ]; then
		clog_check_syntax $CLOG_FAILED "$APACHE_config_file"
		return $OCF_ERR_GENERIC
	fi

	clog_check_syntax $CLOG_SUCCEED "$APACHE_config_file"

	return 0
}

generate_configFile()
{
	declare originalConfigFile=$1;
	declare generatedConfigFile=$2;
	declare ip_addresses=$3;

	if [ -f "$generatedConfigFile" ]; then
		sha1_verify "$generatedConfigFile"
		if [ $? -ne 0 ]; then
			clog_check_sha1 $CLOG_FAILED
			return 0
		fi
	fi	

	clog_generate_config $CLOG_INIT "$originalConfigFile" "$generatedConfigFile"

	generate_configTemplate "$generatedConfigFile" "$1"
	cat >> "$generatedConfigFile" << EOT
# From a cluster perspective, the key fields are:
#     Listen - must be set to service floating IP address.
#     ServerRoot - path to the ServerRoot (initial value is set in service conf)
#

EOT

	IFS_old="$IFS"
	IFS=$'\n'
	for i in `"$APACHE_parseConfig" -D"$OCF_RESKEY_name" < "$originalConfigFile" | grep -P '(^Listen)|(^Port)' | grep -v ':'`; do 
		port=`echo $i | sed 's/^Listen \(.*\)/\1/;s/^Port \(.*\)/\1/'`;
		IFS=$' ';
		for z in $ip_addresses; do 
			echo "Listen $z:$port" >> "$generatedConfigFile";
		done
		IFS=$'\n';
	done;
	IFS="$IFS_old"

	echo "PidFile \"$APACHE_pid_file\"" >> "$generatedConfigFile";
	echo >> "$generatedConfigFile"

	cat "$originalConfigFile" | sed 's/^Listen/### Listen/;s/^Port/### Port/;s/^PidFile/### PidFile/' | \
	"$APACHE_parseConfig" -D"$OCF_RESKEY_name" >> "$generatedConfigFile"

	sha1_addToFile "$generatedConfigFile"
	clog_generate_config $CLOG_SUCCEED "$originalConfigFile" "$generatedConfigFile"
}

start()
{
	declare ccs_fd
	declare ip_addresses

	clog_service_start $CLOG_INIT	

	create_pid_directory
	create_conf_directory "$APACHE_conf_dir"
	check_pid_file "$APACHE_pid_file"

	if [ $? -ne 0 ]; then
		clog_check_pid $CLOG_FAILED "$APACHE_pid_file"
		clog_service_start $CLOG_FAILED
		return $OCF_ERR_GENERIC
	fi

	clog_looking_for $CLOG_INIT "IP Addresses"

	ccs_fd=$(ccs_connect);
	if [ $? -ne 0 ]; then
		clog_looking_for $CLOG_FAILED_CCS
		return $OCF_ERR_GENERIC
	fi
	
	get_service_ip_keys "$ccs_fd" "$OCF_RESKEY_service_name"
	ip_addresses=`build_ip_list "$ccs_fd"`

	if [ -z "$ip_addresses" ]; then
		clog_looking_for $CLOG_FAILED_NOT_FOUND "IP Addresses"
		return $OCF_ERR_GENERIC
	fi
	
	clog_looking_for $CLOG_SUCCEED "IP Addresses"

	generate_configFile "$APACHE_serverConfigFile" "$APACHE_genConfig" "$ip_addresses"

	"$APACHE_HTTPD" \
		"-D$OCF_RESKEY_name" \
		-d "$OCF_RESKEY_server_root" \
		-f "$APACHE_genConfig" \
		$OCF_RESKEY_httpd_options \
		-k start

	if [ $? -ne 0 ]; then
		clog_service_start $CLOG_FAILED		
		return $OCF_ERR_GENERIC
	else
		clog_service_start $CLOG_SUCCEED
	fi

	return 0;
}

stop()
{
	clog_service_stop $CLOG_INIT

	stop_generic "$APACHE_pid_file" "$OCF_RESKEY_shutdown_wait"
	
	if [ $? -ne 0 ]; then
		clog_service_stop $CLOG_FAILED
		return $OCF_ERR_GENERIC
	fi
	
	clog_service_stop $CLOG_SUCCEED
	return 0;
}

status()
{
	clog_service_status $CLOG_INIT

	status_check_pid "$APACHE_pid_file"
	if [ $? -ne 0 ]; then
		clog_service_status $CLOG_FAILED "$APACHE_pid_file"
		return $OCF_ERR_GENERIC
	fi

	clog_service_status $CLOG_SUCCEED
	return 0
}

if [ "$1" != "meta-data" ]; then
	apache_serverConfigFile
fi;
		
case $1 in
	meta-data)
		cat `echo $0 | sed 's/^\(.*\)\.sh$/\1.metadata/'`
		exit 0
		;;
	validate-all|verify-all)
		verify_all
		exit $?
		;;
	start)
		verify_all && start
		exit $?
		;;
	stop)
		verify_all && stop
		exit $?
		;;
	status|monitor)
		verify_all
		status
		exit $?
		;;
	restart)
		verify_all
		stop
		start
		exit $?
		;;
	*)
		echo "Usage: $0 {start|stop|status|monitor|restart|meta-data|validate-all}"
		exit $OCF_ERR_UNIMPLEMENTED
		;;
esac
