
Csync2 for Win32 (cygwin)
=========================

The Csync2 homepage can be reached at <http://oss.linbit.com/>.

  LINBIT Information Technologies GmbH <http://www.linbit.com>
  Copyright (C) 2004, 2005  Clifford Wolf <clifford@clifford.at>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

The "Csync2 hint daemon for win32" needs a .NET runtime. It can be downloaded
from the microsoft webpage:

  http://www.microsoft.com/downloads/details.aspx?FamilyID=d7158dee-a83f-4e21-b05a-009d06457787&displaylang=en

"Csync2 for Win32 (cygwin)" is based on the free software libraries Librsync,
SQLite, SQLite.NET wrapper, OpenSSL and Cygwin.


Setup:
======

1. Extract the contents of this .ZIP archive to c:\csync2.

2. Create a c:\tmp directory.

3. Create a c:\csync2\csync2.cfg (or copy it from existing nodes).

4. Add entries to c:\winnt\system32\drivers\etc\hosts (optional).

5. Run c:\csync2\monitor.exe in a command window.

Pass the directory names which should be monitored by the hint deamon(s) as
parameters to "monitor.exe".

Read the csync2 documentation (bundled with the csync2 source tar) for more
information about writing csync2 configuration files.

