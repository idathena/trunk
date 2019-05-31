#!/usr/bin/perl

# Item Database:
#     --i=../db/pre-re/item_db.txt --o=../sql-files/item_db.sql --t=pre --m=item --table=item_db
#     --i=../db/re/item_db.txt --o=../sql-files/item_db_re.sql --t=re --m=item --table=item_db_re
#
#     --i=../db/item_db2.txt --o=../sql-files/item_db2.sql --t=pre --m=item --table=item_db2
#     --i=../db/item_db2.txt --o=../sql-files/item_db2_re.sql --t=re --m=item --table=item_db2_re
#
# Mob Skill Database:
#     --i=../db/pre-re/mob_skill_db.txt --o=../sql-files/mob_skill_db.sql --t=pre --m=mob_skill --table=mob_skill_db
#     --i=../db/re/mob_skill_db.txt --o=../sql-files/mob_skill_db_re.sql --t=re --m=mob_skill --table=mob_skill_db_re
#
#     --i=../db/mob_skill_db2.txt --o=../sql-files/mob_skill_db2.sql --t=pre --m=mob_skill --table=mob_skill_db2
#     --i=../db/mob_skill_db2.txt --o=../sql-files/mob_skill_db2_re.sql --t=re --m=mob_skill --table=mob_skill_db2_re
#
# List of options:
#   convert_sql.pl --help

use strict;
use warnings;
use Getopt::Long;
use File::Basename;

my $sFilein = "";
my $sFileout = "";
my $sTarget = "";
my $sType = "";
my $sHelp = 0;
my $sTable = "";

my $db;
my $nb_columns;
my @str_col = (); #Use basic escape.
my @str_col2 = (); #Use second escape (currently for scripts).
my $line_format;
my $create_table;
my @defaults = ();

Main();

sub GetArgs {
	GetOptions(
	'i=s' => \$sFilein, #Output file name.
	'o=s' => \$sFileout, #Input file name.
	't=s' => \$sTarget, #Renewal setting: pre-re, re.
	'm=s' => \$sType, #Database: item, mob_skill.
	'table=s' => \$sTable, #Table name.
	'help!' => \$sHelp,
	) or $sHelp=1; #Display help if invalid options are supplied.
	my $sValidTarget = "re|pre";
	my $sValidType = "item|mob_skill";

	if( $sHelp ) {
		print "Incorrect option specified. Available options:\n"
			."\t --o=filename => Output file name. \n"
			."\t --i=filename => Input file name. \n"
			."\t --table=tablename => Table name to create. \n"
			."\t --t=target => Specify target ([$sValidTarget]). \n"
			."\t --m=type => Specify type ([$sValidType]). \n";
		exit;
	}
	unless($sFilein or $sFileout){
		print "ERROR: Filename_in and Filename_out are required to continue.\n";
		exit;
	}
	unless($sTarget =~ /$sValidTarget/i){
		print "ERROR: Incorrect target specified. Available targets:\n"
			."\t --t => Target (specify which kind of table_struct to build [$sValidTarget]).\n";
		exit;
	}
	unless($sType =~ /$sValidType/i){
		print "ERROR: Incorrect type specified. Available types:\n"
			."\t --m => Type (specify which data entry to use [$sValidType]).\n";
		exit;
	}
}

sub Main {
	GetArgs();
	my($filename, $dir, $suffix) = fileparse($0);
	chdir $dir; #put ourself like was called in tool folder
	BuildDataForType($sTarget,$sType);
	ConvertFile($sFilein,$sFileout,$sType);
	print "Conversion ended.\n";
}

sub ConvertFile { my($sFilein,$sFileout,$sType)=@_;
	my $sFHout;
	my %hAEgisName = ();
	print "Starting ConvertFile with: \n\t filein=$sFilein \n\t fileout=$sFileout \n";
	open FHIN,"$sFilein" or die "ERROR: Can't read or locate $sFilein.\n";
	open $sFHout,">$sFileout" or die "ERROR: Can't write $sFileout.\n";
	
	printf $sFHout ("%s\n",$create_table);
	while(my $ligne=<FHIN>) {
		my $sWasCom = 0;
		if ($ligne =~ /^\s*$/ ) {
				print $sFHout "\n";
				next;
		}
		if ($ligne =~ /[^\r\n]+/) {
			$ligne = $&;
			if ($ligne =~ /^\/\//) {
				printf $sFHout ("#");
				$ligne = substr($ligne, 2);
				$sWasCom = 1;
			}
			my @champ = ();
			if ($sType =~ /mob_skill/i ) {
				if ($ligne =~ $line_format ) { @champ = ($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14,$15,$16,$17,$18,$19); }
			}
			elsif ($sType =~ /item/i ) {
				if ($ligne =~ $line_format) { @champ = ($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14,$15,$16,$17,$18,$19,$20,$21,$22); } 
			}
			if ($#champ != $nb_columns - 1) { #Can't parse, it's a real comment.
				printf $sFHout ("%s\n", $ligne);
			} else {
				printf $sFHout ("REPLACE INTO `%s` VALUES (", $db);
				if( $sType =~ /item/i and $sWasCom == 0){ #check if aegis name is duplicate, (only for not com)
					$hAEgisName{$champ[1]}++;
					if($hAEgisName{$champ[1]} > 1){
						print "Warning, aegisName=$champ[1] multiple occurence found, val=$hAEgisName{$champ[1]}, line=$ligne\n" ;
						$champ[1] = $champ[1]."_"x($hAEgisName{$champ[1]}-1);
						print "Converted into '$champ[1]'\n" ;
					}
				}				 
				for (my $i=0; $i<$#champ; $i++) {
					printField($sFHout,$champ[$i],",",$i);
				}
				printField($sFHout,$champ[$#champ],");\n",$#champ);
			}
		}
	}
	print $sFHout "\n";
}

sub printField { my ($sFHout,$str, $suffix, $idCol) = @_;
	# Remove first { and last } .
	if ($str =~ /{.*}/) {
		$str = substr($&,1,-1);
	}
	# If nothing, put NULL.
	if ($str eq "") {
		my $sDef;
		if(scalar(@defaults)) { $sDef = $defaults[$idCol]; } #Use default in array.
		else { $sDef = "NULL" unless($sDef); } #Let SQL handle the default.
		print $sFHout "$sDef$suffix";
	} else {
		my $flag = 0;
		# Search if it's a string column?
		foreach my $col (@str_col) {
			if ($col == $idCol) {
				$flag |= 1;
				last;
			}
		}
		foreach my $col (@str_col2) {
			if ($col == $idCol) {
				$flag |= 2;
				last;
			}
		}

		if ($flag & 3) {
			# String column, so escape , remove trailing and add '' .
			my $string;
			$string = escape($str,"'","\\'") if($flag & 1) ;
			$string =~ s/\s+$//; #Remove trailing spaces.
			$string =~ s/^\s+//; #Remove leading spaces.
			$string = escape($string,'\\\"','\\\\\"') if($flag & 2) ;
			printf $sFHout ("'%s'%s", $string, $suffix);
		} else {
			# Not a string column.
			printf $sFHout ("%s%s", $str,$suffix);
		}
	}
}

sub escape { my ($str,$sregex,$sreplace) = @_;
	my @str_splitted = split($sregex, $str);
	my $result = "";
	for (my $i=0; $i<=$#str_splitted; $i++) {
		if ($i == 0) {
			$result = $str_splitted[0];
		} else {
			$result = $result.$sreplace.$str_splitted[$i];
		}
	}
	return $result
}

sub BuildDataForType{ my($sTarget,$sType) = @_;
	print "Starting BuildDataForType with: \n\t Target=$sTarget, Type=$sType \n";
	
	if($sType =~ /item/i) {
		if($sTarget =~ /Pre/i){
			$db = $sTable;
			$db = "item_db" unless($db);
			$nb_columns = 22;
			@str_col = (1,2,19,20,21);
			@str_col2 = (19,20,21);
			$line_format = "([^\,]*),"x($nb_columns-3)."(\{.*\}),"x(2)."(\{.*\})"; #Last 3 columns are scripts.
			$create_table =
"#
# Table structure for table `$db`
#

DROP TABLE IF EXISTS `$db`;
CREATE TABLE `$db` (
  `id` smallint(5) unsigned NOT NULL default '0',
  `name_english` varchar(50) NOT NULL default '',
  `name_japanese` varchar(50) NOT NULL default '',
  `type` tinyint(2) unsigned NOT NULL default '0',
  `price_buy` mediumint(8) unsigned default NULL,
  `price_sell` mediumint(8) unsigned default NULL,
  `weight` smallint(5) unsigned NOT NULL default '0',
  `attack` smallint(5) unsigned default NULL,
  `defence` smallint(5) unsigned default NULL,
  `range` tinyint(2) unsigned default NULL,
  `slots` tinyint(2) unsigned default NULL,
  `equip_jobs` bigint(20) unsigned default NULL,
  `equip_upper` tinyint(2) unsigned default NULL,
  `equip_genders` tinyint(1) unsigned default NULL,
  `equip_locations` mediumint(7) unsigned default NULL,
  `weapon_level` tinyint(1) unsigned default NULL,
  `equip_level` tinyint(3) unsigned default NULL,
  `refineable` tinyint(1) unsigned default NULL,
  `view` smallint(5) unsigned default NULL,
  `script` text,
  `equip_script` text,
  `unequip_script` text,
  PRIMARY KEY (`id`),
  UNIQUE INDEX `UniqueAegisName` (`name_english`)
) ENGINE=MyISAM;
";
		#NOTE: These do not match the table struct defaults.
		@defaults = ('0','\'\'','\'\'','0','NULL','NULL','NULL','NULL','NULL','NULL','NULL','NULL','NULL','NULL','NULL','NULL','NULL','NULL','NULL','NULL','NULL','NULL');
		}
		elsif($sTarget =~ /Re/i){
			$db = $sTable;
			$db = "item_db_re" unless($db);
			$nb_columns = 22;
			@str_col = (1,2,7,16,19,20,21);
			@str_col2 = (19,20,21);
			$line_format = "([^\,]*),"x($nb_columns-3)."(\{.*\}),"x(2)."(\{.*\})"; #Last 3 columns are scripts.
			$create_table =
"#
# Table structure for table `$db`
#

DROP TABLE IF EXISTS `$db`;
CREATE TABLE `$db` (
  `id` smallint(5) unsigned NOT NULL default '0',
  `name_english` varchar(50) NOT NULL default '',
  `name_japanese` varchar(50) NOT NULL default '',
  `type` tinyint(2) unsigned NOT NULL default '0',
  `price_buy` mediumint(8) unsigned default NULL,
  `price_sell` mediumint(8) unsigned default NULL,
  `weight` smallint(5) unsigned NOT NULL default '0',
  `atk:matk` varchar(11) default NULL,
  `defence` smallint(5) unsigned default NULL,
  `range` tinyint(2) unsigned default NULL,
  `slots` tinyint(2) unsigned default NULL,
  `equip_jobs` bigint(20) unsigned default NULL,
  `equip_upper` tinyint(2) unsigned default NULL,
  `equip_genders` tinyint(1) unsigned default NULL,
  `equip_locations` mediumint(7) unsigned default NULL,
  `weapon_level` tinyint(1) unsigned default NULL,
  `equip_level` varchar(10) default NULL,
  `refineable` tinyint(1) unsigned default NULL,
  `view` smallint(5) unsigned default NULL,
  `script` text,
  `equip_script` text,
  `unequip_script` text,
  PRIMARY KEY (`id`),
  UNIQUE INDEX `UniqueAegisName` (`name_english`)
) ENGINE=MyISAM;
";
		#NOTE: These do not match the table struct defaults.
		@defaults = ('0','\'\'','\'\'','0','NULL','NULL','NULL','NULL','NULL','NULL','NULL','NULL','NULL','NULL','NULL','NULL','NULL','NULL','NULL','NULL','NULL','NULL');
		}
	}
	elsif($sType =~ /mob_skill/i) { #Same format for Pre-Renewal and Renewal.
		$db = $sTable;
		if($sTarget =~ /Pre/i){
			$db = "mob_skill_db" unless($db);
		}else{
			$db = "mob_skill_db_re" unless($db);
		}
		$nb_columns = 19;
		@str_col = (1,2,8,9,10,11,17,18);
		$line_format = "([^\,]*),"x($nb_columns-1)."([^\,]*)";
		$create_table =
"#
# Table structure for table `$db`
#

DROP TABLE IF EXISTS `$db`;
CREATE TABLE IF NOT EXISTS `$db` (
  `MOB_ID` smallint(6) NOT NULL,
  `INFO` text NOT NULL,
  `STATE` text NOT NULL,
  `SKILL_ID` smallint(6) NOT NULL,
  `SKILL_LV` tinyint(4) NOT NULL,
  `RATE` smallint(4) NOT NULL,
  `CASTTIME` mediumint(9) NOT NULL,
  `DELAY` int(9) NOT NULL,
  `CANCELABLE` text NOT NULL,
  `TARGET` text NOT NULL,
  `CONDITION` text NOT NULL,
  `CONDITION_VALUE` text,
  `VAL1` mediumint(9) default NULL,
  `VAL2` mediumint(9) default NULL,
  `VAL3` mediumint(9) default NULL,
  `VAL4` mediumint(9) default NULL,
  `VAL5` mediumint(9) default NULL,
  `EMOTION` text,
  `CHAT` text
) ENGINE=MyISAM;
";
	}
}
