#!/usr/bin/perl -w
# Copyright: telegraaf (NL)
# Author: ard@telegraafnet.nl
# License: GPL v2 or higher
use strict;
use Linux::Inotify2;
use Data::Dumper;
use File::Find;
use POSIX qw(uname :sys_wait_h);
use Sys::Syslog;
use Net::Server::Daemonize qw(daemonize);
use IO::Select;
use Fcntl;


my $program="csync2id";
my $daemonize=1;
my $usesyslog=1;
my $pidfile='/var/run/csync2id.pid';
my $pidfileboot='/var/run/csync2id.boot.pid';

################################################################################
# Config items
# Overridden by /etc/csync2id.cfg
# Normal config in /etc/csync2id.cfg:
#
# @::dirs=qw( /data1 /data2 );
# 1;
#
# csyncdirhint:  preferred hint command for directories (a single directory name
#                will be added)
# csyncfilehint: preferred hint command for files (at most $x filenames will be appended)
# csynccheck: preferred command scheduled right after the hint, or after a timeout
# csyncupdate: preferred command scheduled right after the check
# debug: log debug lines
# statsdir: file to log the number of watched directories
# statchanges: file to log the number of file change events
# statsretry: file to log the number of retries needed so far for the hint
# dirs: an array of directories which need to be watched recursively
################################################################################

$::csynchintmaxargs=20;
@::csyncdirhint=("/usr/sbin/csync2", "-B","-A","-rh");
@::csyncfilehint=("/usr/sbin/csync2", "-B","-A","-h");
@::csynccheck=("/usr/sbin/csync2", "-B","-A","-c");
@::csyncupdate=("/usr/sbin/csync2", "-B","-A","-u");
$::debug=3;
$::statsdir="/dev/shm/csyncstats/dirs";
$::statschanges="/dev/shm/csyncstats/changes";
$::statsretry="/dev/shm/csyncstats/retry";
@::dirs=();
require "/etc/csync2id.cfg";

$daemonize && daemonize(0,0,$pidfileboot);
$usesyslog && openlog("$program",'pid','daemon');

use constant { LOGERR => 0, LOGWARN => 1, LOGINFO =>2, LOGDEBUG=>3,LOGSLOTS=>256 };
my %prios=( 0 => 'err', 1 => 'warning', 2 => 'info', default => 'debug' );
sub logger {
	my($level,@args)=@_;
	my ($prio)=$prios{$level}||$prios{'default'}; # :$prios{'default'};
	if($usesyslog) {
		syslog($prio,@args) if (($level<= LOGDEBUG && $level<=$::debug)||($::debug>=LOGDEBUG && $level&$::debug)) 
	} else {
		print "LOG: $prio ";
		print(@args);
		print "\n";
	}
}

logger(LOGDEBUG,Dumper(\@::dirs));


my $inotify = new Linux::Inotify2 or ( logger(LOGERR, "Unable to create new inotify object: $!") && die("inotify") );

# For stats
my $globaldirs=0;
my $globalevents=0;
my $globalhintretry=0;

sub logstatsline {
	my ($file,$line)=@_;
#	open STATS,"> $file";
#	print STATS $line;
#	close STATS;
}


#package Runner;
################################################################################
# Process runner
# Runs processes and keep status
# API:
# runstatus: current status of a runslot (running/idle)
# exitstatus: last status of an exec
# slotrun: forkexec a new command with a callback when it's finished for a specific slot
# Helpers:
# reaper is the SIGCHLD handler
# checkchildren should be called after syscalls which exited with E_INTR, and
# calls the specific callbacks.
################################################################################
use constant { RUN_IDLE => 0, RUN_RUNNING => 1, RUN_REAPED =>2 };
my %slotstatus;
my %slotexitstatus;
my %slotcommandline;
my %slotcallback;
my %slotpid2slot;
my %slotstarttime;

# pid queue for reaper
# Every pid (key)  contains a waitforpid exit status as value.
my %slotpidreaped;

sub runstatus {
	my ($slot)=@_;
	return($slotstatus{$slot}) if exists($slotstatus{$slot});
	return RUN_IDLE;
}
sub slotrun {
	my ($slot,$callback,$commandline)=(@_);
	$SIG{CHLD} = \&reaper;
	if(runstatus($slot)!=RUN_IDLE) {
		logger(LOGDEBUG,"SlotRun: Asked to run for $slot, but $slot != RUN_IDLE");
		return -1;
	}
	$slotcommandline{$slot}=$commandline;
	$slotcallback{$slot}=$callback;
	$slotstatus{$slot}=RUN_RUNNING;
	$slotstarttime{$slot}=time();
	my $pid=fork();
	if(!$pid) {
		# We know that exec should not return. Now tell the perl interpreter that we know.
		{
			exec(@$commandline);
		}
		logger(LOGWARN,"SlotRun: $slot Exec failed: ".join(' ','>', @$commandline,'<'));
		# If we can't exec, we don't really know why, and we don't want to go busy fork execing
		# Give a fork exec grace by waiting
		sleep 1;
		exit 1;
	}
	logger(LOGDEBUG,"SlotRun: $slot # ".$pid.": run".join(' ','>', @$commandline,'<'));
	$slotpid2slot{$pid}=$slot;
}
sub exitstatus {
	my ($slot)=@_;
	return($slotexitstatus{$slot}) if exists($slotexitstatus{$slot});
	return -1;
}
sub reaper {
}

sub checkchildren {
	if($::debug==LOGSLOTS) {
		while(my ($slot,$status) = each %slotstatus) {
			logger(LOGDEBUG,"SlotRun: $slot status $status time: ".($status?(time()-$slotstarttime{$slot}):'x'));
		};
	}
	while() {
		my ($pid)=waitpid(-1,&WNOHANG);
		if($pid<=0) {
			last;
		}
		my $status=$?;
		if (WIFEXITED($status)||WIFSIGNALED($status) && exists($slotpid2slot{$pid})) {
			my $slot=$slotpid2slot{$pid};
			delete($slotpid2slot{$pid});
			$slotstatus{$slot}=RUN_IDLE;
			$slotexitstatus{$slot}=$status;
			logger(LOGDEBUG, "SlotRun: $slot $pid exited with $status == ".WEXITSTATUS($status).".\n");
			# Callback determines if we run again or not.
			$slotcallback{$slot}->($slot,$slotexitstatus{$slot},$slotcommandline{$slot});
		} else {
			logger(LOGDEBUG, "SlotRun: Unknown process $pid change state.\n");
		}
	}
}






################################################################################
# CSYNC RUNNERS
# groups queued hints into single csync commands
# run csync update and check commands 
################################################################################

# use constant { CSYNCHINT => 0 , CSYNCCHECK=>1 , CSYNCUPDATE=>2 };
my @hintfifo;

sub updateCallback {
	my ($slot,$exitstatus,$command)=@_;
	if($exitstatus) {
		logger(LOGWARN,"Updater got ".$exitstatus.", NOT retrying run:".join(' ','>',@$command,'<'));
	}
}
sub runupdater {
	if(runstatus('csupdate') == RUN_IDLE) {
		slotrun('csupdate',\&updateCallback,\@::csyncupdate);
	}
}

sub checkerCallback {
	my ($slot,$exitstatus,$command)=@_;
	if($exitstatus) {
		logger(LOGWARN,"Checker got ".$exitstatus.", NOT retrying run:".join(' ','>',@$command,'<'));
	}
	runupdater();
}
sub runchecker {
	if(runstatus('cscheck') == RUN_IDLE) {
		slotrun('cscheck',\&checkerCallback,\@::csynccheck);
	}
}
sub hinterCallback {
	my ($slot,$exitstatus,$command)=@_;
	if($exitstatus) {
		logger(LOGWARN,"Hinter got ".$exitstatus.", retrying run:".join(' ','>',@$command,'<'));
		$globalhintretry++;
		logstatsline($::statsretry,$globalhintretry);
		slotrun($slot,\&hinterCallback,$command);
	} else {
		runchecker();
	}
}
sub givehints {
	if(runstatus('cshint') == RUN_IDLE && @hintfifo) {
		# PREPARE JOB
		# Directories should be treated with care, one at a time.
		my @hintcommand;
		if($hintfifo[0]->{'recurse'}) {
			my $filename=$hintfifo[0]->{'filename'};
			@hintcommand=(@::csyncdirhint,$filename);
			shift(@hintfifo) while (@hintfifo && $filename eq $hintfifo[0]->{'filename'} );
		} else {
			# Files can be bulked, until the next directory
			my $nrargs=0;
			@hintcommand=(@::csyncfilehint);
			while($nrargs < $::csynchintmaxargs && @hintfifo && !$hintfifo[0]->{'recurse'}) {
				my $filename=$hintfifo[0]->{'filename'};
				push(@hintcommand,$filename);
				shift(@hintfifo) while (@hintfifo && $filename eq $hintfifo[0]->{'filename'} );
				$nrargs++;
			}
		}
		slotrun('cshint',\&hinterCallback,\@hintcommand);
	}
}

################################################################################
# Subtree parser
# Adds subtrees to an existing watch
# globals: $globaldirs for stats.
# Logs to logger
################################################################################
sub watchtree {
	my ($inotifier,$tree,$inotifyflags) = @_;
	$inotifier->watch ($tree, $inotifyflags);
	$globaldirs++;
	find(
		sub {
			if(! m/^\.\.?$/) {
				my ($dev, $ino, $mode, $nlink, $uid, $gid) = lstat($_) ;
				if(-d _ ) {
					if ($nlink==2) {
						$File::Find::prune = 1;
					}
					$inotifier->watch ($File::Find::dir.'/'.$_, $inotifyflags) or die("WatchTree: watch creation failed (maybe increase the number of watches?)");
					$globaldirs++;
					logger(LOGDEBUG,"WatchTree: directory ". $globaldirs." ".$File::Find::dir.'/'.$_);
				}
			}
		},
		$tree
	);
	logstatsline($::statsdir,$globaldirs);
}


################################################################################
# Main
#
logger(LOGINFO, 'Main: Starting $Id: csync2id.pl,v 1.18 2008/12/24 15:34:19 ard Exp $');
# Start watching the directories
logger(LOGINFO, "Main: traversing directories");
eval {
	watchtree($inotify,$_,IN_MOVE|IN_DELETE|IN_CLOSE_WRITE|IN_ATTRIB|IN_CREATE) foreach(@::dirs)
};
if($@) {
	logger(LOGERR,"Main: $@");
	exit(2);
}
logger(LOGINFO,"Main: ready for events");

# Kill other daemon because we are ready
if($daemonize) {
	if ( -e $pidfile ) {
		my $thepid;
		@ARGV=($pidfile);
		$thepid=<>;
		logger(LOGINFO, "Main: about to kill previous incarnation $thepid");
		kill(15,$thepid);
		sleep 0.5;
	}
	rename($pidfileboot,$pidfile);
}

# Main loop
$inotify->blocking(O_NONBLOCK);
my $timeout=20;
while () {
	#my ($rhset,$dummy,$dummy,$timeleft)=IO::Select->select($selectset, undef, undef, 60);
	my $nfound;
	my $rin='';
	vec($rin,$inotify->fileno,1)=1;
	($nfound,$timeout)=select($rin, undef, undef, $timeout);
	logger(LOGDEBUG,"Main: nrfds: $nfound timeleft: $timeout\n");
	if(!$timeout) {
		$timeout=20;
		logger(LOGDEBUG, "Main: timeout->check and update");
		runchecker();
		runupdater();
		# 
	}
	if($nfound>0) {
		my @events = $inotify->read;
		unless (@events > 0) {
			logger(LOGWARN,"Main: Zero events, must be a something weird");
		}
		foreach(@events) {
			if($_->IN_Q_OVERFLOW) {
				logger(LOGERR,"Main: FATAL:inotify queue overflow: csync2id was to slow to handle events");
			}
			if( $_->IN_ISDIR) {
				my $recurse=0;
				# We want to recurse only for new, renamed or deleted directories
				$recurse=$_->IN_DELETE||$_->IN_CREATE||$_->IN_MOVED_TO||$_->IN_MOVED_FROM;
				eval watchtree($inotify,$_->fullname,IN_MOVE|IN_DELETE|IN_CLOSE_WRITE|IN_ATTRIB|IN_CREATE) if $_->IN_CREATE||$_->IN_MOVED_TO;
				if($@) {
					logger(LOGINFO,"$@");
					exit(3);
				}
				push(@hintfifo,{ "filename" => $_->fullname , "recurse" => $recurse });
				logger(LOGDEBUG,"Main: dir: ".$_->mask." ".$recurse." ".$_->fullname);
			} else {
				# Accumulate single file events:
				next if(@hintfifo && $hintfifo[-1]->{"filename"} eq $_->fullname);
				push(@hintfifo,{ "filename" => $_->fullname , "recurse" => 0 });
				logger(LOGDEBUG,"Main: file: ".$_->mask," ".$_->fullname);
			}
			$globalevents++;
		}
	}
	checkchildren();
	givehints();
	logstatsline($::statschanges,$globalevents);
}
