# Csync2 Example Configuration File
# ---------------------------------
#
# Please read the documentation:
# http://oss.linbit.com/csync2/paper.pdf

# group mygroup
# {
# 	host host1 host2 (host3);
# 	host host4@host4-eth2;
#
# 	key /etc/csync2.key_mygroup;
#
# 	#
# 	# WARNING:
# 	# You CANNOT use paths containing a symlink
# 	# component in include/exclude options!
# 	#
# 	# Here is a real-life example:
# 	# Suppose you have some 64bit Linux systems
# 	# and /usr/lib/ocf is what you want to keep
#	# in sync. On 64bit Linux systems, /usr/lib
# 	# is usually a symlink to /usr/lib64.
# 	# This does not work:
# 	#   include /usr/lib/ocf;
# 	# But this does work:
# 	#   include /usr/lib64/ocf;
# 	#
# 
# 	include /etc/apache;
# 	include %homedir%/bob;
# 	exclude %homedir%/bob/temp;
# 	exclude *~ .*;
#
# 	action
# 	{
# 		pattern /etc/apache/httpd.conf;
# 		pattern /etc/apache/sites-available/*;
# 		exec "/usr/sbin/apache2ctl graceful";
# 		logfile "/var/log/csync2_action.log";
# 		do-local;
#		# you can use do-local-only if the execution
#		# should be done locally only
#		# do-local-only;
# 	}
#
# 	# The backup-directory needs to be created first!
# 	backup-directory /var/backups/csync2;
# 	backup-generations 3;
#
# 	auto none;
# }
#
# prefix homedir
# {
# 	on host[12]: /export/users;
# 	on *:        /home;
# }
