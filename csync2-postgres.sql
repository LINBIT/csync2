--
-- Table structure for table action
--

DROP TABLE IF EXISTS action;
CREATE TABLE action (
  filename varchar(255) DEFAULT NULL,
  command text,
  logfile text,
  UNIQUE (filename,command)
);

--
-- Table structure for table dirty
--

DROP TABLE IF EXISTS dirty;
CREATE TABLE dirty (
  filename varchar(200) DEFAULT NULL,
  forced int DEFAULT NULL,
  myname varchar(100) DEFAULT NULL,
  peername varchar(100) DEFAULT NULL,
  UNIQUE (filename,peername)
);

--
-- Table structure for table file
--

DROP TABLE IF EXISTS file;
CREATE TABLE file (
  filename varchar(200) DEFAULT NULL,
  checktxt varchar(200) DEFAULT NULL,
  UNIQUE (filename)
);

--
-- Table structure for table hint
--

DROP TABLE IF EXISTS hint;
CREATE TABLE hint (
  filename varchar(255) DEFAULT NULL,
  recursive int DEFAULT NULL
);

--
-- Table structure for table x509_cert
--

DROP TABLE IF EXISTS x509_cert;
CREATE TABLE x509_cert (
  peername varchar(255) DEFAULT NULL,
  certdata varchar(255) DEFAULT NULL,
  UNIQUE (peername)
);
