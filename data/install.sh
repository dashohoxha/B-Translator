#!/bin/bash

### go to the script directory
cd $(dirname $0)

### get the DB connection parameters
mysql_params="$($(which php) db/get-connection.php bash)"
#echo $mysql_params;  exit 0;  ## debug

### create the DB tables
mysql $mysql_params < db/l10n_feedback_schema.sql

### get the PO files and import them
./update.sh