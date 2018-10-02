#!/usr/bin/perl
# This Script converts quest_db.txt to quest_db.conf format.
# usage example: perl tools/convert_questdb.pl < db/quest_db.txt > db/quest_db.conf
#

use strict;
use warnings;

sub parse_questdb (@) {
	my @input = @_;
	foreach (@input) {
		chomp $_;
#		Quest ID,Time Limit,Target1,Val1,Target2,Val2,Target3,Val3,MobID1,ItemID1,Rate1,MobID2,ItemID2,Rate2,MobID3,ItemID3,Rate3,Quest Title
		if( $_ =~ qr/^
			(?<prefix>(?:\/\/[^0-9]*)?)
			(?<QuestID>[0-9]+)[^,]*,[\s\t]*
			(?<TimeLimit>[0-9]+)[^,]*,[\s\t]*
			(?<Target1>[0-9]+)[^,]*,[\s\t]*
			(?<Val1>[0-9]+)[^,]*,[\s\t]*
			(?<Target2>[0-9]+)[^,]*,[\s\t]*
			(?<Val2>[0-9]+)[^,]*,[\s\t]*
			(?<Target3>[0-9]+)[^,]*,[\s\t]*
			(?<Val3>[0-9]+)[^,]*,[\s\t]*
			(?<MobID1>[0-9]+)[^,]*,[\s\t]*
			(?<ItemID1>[0-9]+)[^,]*,[\s\t]*
			(?<Rate1>[0-9]+)[^,]*,[\s\t]*
			(?<MobID2>[0-9]+)[^,]*,[\s\t]*
			(?<ItemID2>[0-9]+)[^,]*,[\s\t]*
			(?<Rate2>[0-9]+)[^,]*,[\s\t]*
			(?<MobID3>[0-9]+)[^,]*,[\s\t]*
			(?<ItemID3>[0-9]+)[^,]*,[\s\t]*
			(?<Rate3>[0-9]+)[^,]*,[\s\t]*
			"(?<QuestTitle>[^"]*)"
		/x ) {
			my %cols = map { $_ => $+{$_} } keys %+;
			print "/*\n" if $cols{prefix};
			print "$cols{prefix}\n" if $cols{prefix} and $cols{prefix} !~ m|^//[\s\t]*$|;
			print "{\n";
			print "\tId: $cols{QuestID}\n";
			print "\tName: \"$cols{QuestTitle}\"\n";
			print "\tTimeLimit: \"$cols{TimeLimit}\"\n" if $cols{TimeLimit};
			print "\tTargets: (\n" if $cols{Target1} || $cols{Target2} || $cols{Target3};
			print "\t{\n" if $cols{Target1};
			print "\t\tMobId: $cols{Target1}\n" if $cols{Target1};
			print "\t\tCount: $cols{Val1}\n" if $cols{Target1};
			print "\t},\n" if $cols{Target2};
			print "\t{\n" if $cols{Target2};
			print "\t\tMobId: $cols{Target2}\n" if $cols{Target2};
			print "\t\tCount: $cols{Val2}\n" if $cols{Target2};
			print "\t},\n" if $cols{Target3};
			print "\t{\n" if $cols{Target3};
			print "\t\tMobId: $cols{Target3}\n" if $cols{Target3};
			print "\t\tCount: $cols{Val3}\n" if $cols{Target3};
			print "\t}\n" if $cols{Target1} || $cols{Target2} || $cols{Target3};
			print "\t)\n" if $cols{Target1} || $cols{Target2} || $cols{Target3};
			print "\tDrops: (\n" if $cols{ItemID1} || $cols{ItemID2} || $cols{ItemID3};
			print "\t{\n" if $cols{ItemID1};
			print "\t\tItemId: $cols{ItemID1}\n" if $cols{ItemID1};
			print "\t\tRate: $cols{Rate1}\n" if $cols{ItemID1};
			print "\t\tMobId: $cols{MobID1}\n" if $cols{ItemID1};
			print "\t},\n" if $cols{ItemID2};
			print "\t{\n" if $cols{ItemID2};
			print "\t\tItemId: $cols{ItemID2}\n" if $cols{ItemID2};
			print "\t\tRate: $cols{Rate2}\n" if $cols{ItemID2};
			print "\t\tMobId: $cols{MobID2}\n" if $cols{ItemID2};
			print "\t},\n" if $cols{ItemID3};
			print "\t{\n" if $cols{ItemID3};
			print "\t\tItemId: $cols{ItemID3}\n" if $cols{ItemID3};
			print "\t\tRate: $cols{Rate3}\n" if $cols{ItemID3};
			print "\t\tMobId: $cols{MobID3}\n" if $cols{ItemID3};
			print "\t}\n" if $cols{ItemID1} || $cols{ItemID2} || $cols{ItemID3};
			print "\t)\n" if $cols{ItemID1} || $cols{ItemID2} || $cols{ItemID3};
			print "},\n";
			print "*/\n" if $cols{prefix};
		} elsif( $_ =~ /^\/\/(.*)$/ ) {
			my $s = $1;
			print "// $s\n" unless $s =~ /^[\s\t]*$/;
		} elsif( $_ !~ /^\s*$/ ) {
			print "// Error parsing: $_\n";
		}

	}
}
print <<"EOF";
//=========================================================================
//= Quests Database
//=========================================================================

/**************************************************************************
 ************* Entry structure ********************************************
 **************************************************************************
{
	Id: (int)                 // Quest ID
	Name: (string)            // Quest Name
	TimeLimit: (string)       // (optional)
	                          // in seconds ; date limit will be at [Current time + TimeLimit]
	                          // in HH:MM format ; date limit will be at [TimeLimit] of the current day or at [TimeLimit]
	Targets: ( (array)        // (optional)
	{
		MobId: (int)      // (optional, defaults to 0) Mob ID
		MobType: (string) // (optional, defaults to 0) MOB_TYPE_SIZE_X ~ MOB_TYPE_DEF_ELE_UNDEAD
		MinLevel: (int)   // (optional, defaults to 0) Minimum Level
		MaxLevel: (int)   // (optional, defaults to 0) Maximum Level
		Count: (int)      // Mob Count
	},
	... (can repeated up to MAX_QUEST_OBJECTIVES times)
	)
	Drops: ( (array)          // (optional)
	{
		ItemId: (int)     // Item ID to drop
		Rate: (int)       // Drop rate
		MobId: (int)      // (optional, defaults to 0) Mob ID to match
	},
	... (can be repeated)
	)
},
**************************************************************************/

quest_db: (

EOF

parse_questdb(<>);

print ")\n";
