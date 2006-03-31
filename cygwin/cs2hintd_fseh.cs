/*
 *  csync2 - cluster synchronization tool, 2nd generation
 *  LINBIT Information Technologies GmbH <http://www.linbit.com>
 *  Copyright (C) 2005  Clifford Wolf <clifford@clifford.at>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

using System;
using System.IO;
using System.Text.RegularExpressions;
using System.Threading;
using System.Collections;

public class Csync2HintDaemonFSEH
{
	private static Queue changed_files = new Queue();

	public static void Main()
	{
		string[] args = System.Environment.GetCommandLineArgs();

		if(args.Length < 2) {
			Console.Error.WriteLine("Usage: {0} directory [ directory [ ... ] ]", args[0]);
			return;
		}

		for (int i=1; i<args.Length; i++)
		{
			FileSystemWatcher watcher = new FileSystemWatcher();

			watcher.Path = args[i];
			watcher.IncludeSubdirectories = true;
			watcher.NotifyFilter =
				NotifyFilters.Attributes	|
				NotifyFilters.CreationTime	|
				NotifyFilters.DirectoryName	|
				NotifyFilters.FileName		|
				NotifyFilters.LastWrite		|
				NotifyFilters.Security		|
				NotifyFilters.Size;

			watcher.Changed += new FileSystemEventHandler(OnChanged);
			watcher.Created += new FileSystemEventHandler(OnChanged);
			watcher.Deleted += new FileSystemEventHandler(OnChanged);
			watcher.Renamed += new RenamedEventHandler(OnRenamed);

			watcher.EnableRaisingEvents = true;
		}

		while (true) {
			for (int i=1; i<args.Length; i++)
				Console.Error.WriteLine("-- cs2hintd_fseh waiting for filesystem events in '{0}' --", args[i]);
			for (int i=0; i<600; i++) {
				Thread.Sleep(1000);
				if (changed_files.Count > 0) WriteFilename();
			}
		}
	}

	private static void WriteFilename()
	{
		Hashtable donethat = new Hashtable();

		while (changed_files.Count > 0)
		{
			string filename = (string)changed_files.Dequeue();

			if (donethat.ContainsKey(filename)) continue;
			donethat.Add(filename, 1);

			filename = Regex.Replace(filename, "^([a-zA-Z]):\\\\", "/cygdrive/$1/" );
			filename = Regex.Replace(filename, "\\\\", "/" );

			Console.WriteLine("+ {0}", filename);
		}
		Console.WriteLine("- COMMIT");
	}

	private static void ScheduleWriteFilename(string filename)
	{
		changed_files.Enqueue(filename);
	}

	private static void OnChanged(object source, FileSystemEventArgs e)
	{
		Console.Error.WriteLine("** FS Event: '{0}' {1}.", e.FullPath, e.ChangeType);
		ScheduleWriteFilename(e.FullPath);
	}

	private static void OnRenamed(object source, RenamedEventArgs e)
	{
		Console.Error.WriteLine("** FS Event: '{0}' renamed to '{1}'.", e.OldFullPath, e.FullPath);
		ScheduleWriteFilename(e.OldFullPath);
		ScheduleWriteFilename(e.FullPath);
	}
}

