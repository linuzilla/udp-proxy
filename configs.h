#pragma once

#define DATABASE_HOST "localhost"
#define DATABASE_USER "user"
#define DATABASE_PASSWORD "password"
#define DATABASE_NAME "db"

#define DATABASE_CREATE_CLASS "CREATE TABLE IF NOT EXISTS `the_class` (" \
				"`sn` int(11) NOT NULL AUTO_INCREMENT," \
				"`wday` tinyint(4) NOT NULL," \
				"`hour_from` tinyint(4) NOT NULL," \
				"`hour_to` tinyint(4) NOT NULL," \
				"PRIMARY KEY (`sn`)" \
				") ENGINE=MyISAM DEFAULT CHARSET=latin1"

#define DATABASE_CREATE_REQUEST "CREATE TABLE IF NOT EXISTS `the_request` (" \
				"`sn` int(11) NOT NULL AUTO_INCREMENT," \
				"`ipaddr` int(10) unsigned NOT NULL," \
				"`account` varchar(255) NOT NULL," \
				"`request_time` datetime NOT NULL," \
				"`last_allow_time` datetime NOT NULL," \
				"`renew_count` int(11) NOT NULL DEFAULT 0," \
				"`expired` enum('N','Y') DEFAULT 'N'," \
				"PRIMARY KEY (`sn`)," \
				"KEY `reqtime` (`request_time`)," \
				"KEY `allowtime` (`last_allow_time`)," \
				"KEY `expired` (`expired`)" \
				") ENGINE=MyISAM DEFAULT CHARSET=latin1"

#define DATABASE_CLASS_QUERY "SELECT wday,hour_from,hour_to FROM the_class"

#define DATABASE_REQUEST_QUERY "SELECT ipaddr,UNIX_TIMESTAMP(last_allow_time) " \
				"FROM the_request " \
				"WHERE last_allow_time > NOW() AND expired='N'"

