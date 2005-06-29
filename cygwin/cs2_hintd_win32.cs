/*
 *  csync2 - cluster synchronisation tool, 2nd generation
 *  LINBIT Information Technologies GmbH <http://www.linbit.com>
 *  Copyright (C) 2004, 2005  Clifford Wolf <clifford@clifford.at>
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
using SQLite.NET;

public class Csync2HintDaemon
{
	private static string dbfile;
	private static string watchdir;
	private static Queue changed_files = new Queue();

	public static void Main()
	{
		string[] args = System.Environment.GetCommandLineArgs();
	 
		if(args.Length != 3) {
			Console.WriteLine("Usage: {0} db-file directory", args[0]);
			return;
		}

		dbfile = args[1];
		watchdir = args[2];

		FileSystemWatcher watcher = new FileSystemWatcher();
		watcher.Path = watchdir;

		watcher.Changed += new FileSystemEventHandler(OnChanged);
		watcher.Created += new FileSystemEventHandler(OnChanged);
		watcher.Deleted += new FileSystemEventHandler(OnChanged);
		watcher.Renamed += new RenamedEventHandler(OnRenamed);

		watcher.EnableRaisingEvents = true;

		while (true) {
			Console.WriteLine("-- csync2 hint daemon waiting for filesystem events in '{0}' --", watchdir);
			for (int i=0; i<600; i++) {
				Thread.Sleep(1000);
				if (changed_files.Count > 0) UpdateDB();
			}
		}
	}

	private static void UpdateDB()
	{
		SQLiteClient db;
		Hashtable donethat = new Hashtable();

		try {
			db = new SQLiteClient(dbfile);
			db.BusyRetryDelay = 700;
			db.BusyRetries = 5000;
			db.Execute("BEGIN TRANSACTION");
		} catch (SQLiteException e) {
			Console.WriteLine("******************************************************");
			Console.WriteLine("Fatal SQLite error (open database): {0}", e.Message);
			Console.WriteLine("******************************************************");
			return;
		}

		while (changed_files.Count > 0)
		{
			string filename = (string)changed_files.Dequeue();

			if (donethat.ContainsKey(filename)) continue;
			donethat.Add(filename, 1);
			
			filename = Regex.Replace(filename, "^([a-z]):\\\\", "/cygdrive/$1/" );
			filename = Regex.Replace(filename, "\\\\", "/" );

			string query = "INSERT INTO hint ( filename, recursive ) VALUES ( '" + filename + "', 0 )";
			try {
				db.Execute(query);
			} catch (SQLiteException e) {
				Console.WriteLine("******************************************************");
				Console.WriteLine("Fatal SQLite error: {0}\nIn: {1}", e.Message, query);
				Console.WriteLine("******************************************************");
			}
			Console.WriteLine("** Added to DB: {0}", filename);
		}

		try {
			db.Execute("COMMIT TRANSACTION");
			db.Close();
		} catch (SQLiteException e) {
			Console.WriteLine("******************************************************");
			Console.WriteLine("Fatal SQLite error (close database): {0}", e.Message);
			Console.WriteLine("******************************************************");
			return;
		}
	}

	private static void ScheduleUpdateDB(string filename)
	{
		changed_files.Enqueue(filename);
	}

	private static void OnChanged(object source, FileSystemEventArgs e)
	{
		Console.WriteLine("** FS Event: " +  e.FullPath + " " + e.ChangeType);
		ScheduleUpdateDB(e.FullPath);
	}

	private static void OnRenamed(object source, RenamedEventArgs e)
	{
		Console.WriteLine("** FS Event: {0} renamed to {1}", e.OldFullPath, e.FullPath);
		ScheduleUpdateDB(e.OldFullPath);
		ScheduleUpdateDB(e.FullPath);
	}
}

