#!/usr/bin/perl
# This Script converts quest_db.txt to quest_db.conf format.
# usage example:
# perl tools/convert_mobdb.pl < db/pre-re/mob_db.txt > db/pre-re/mob_db.conf
# perl tools/convert_mobdb.pl < db/re/mob_db.txt > db/re/mob_db.conf
# perl tools/convert_mobdb.pl < db/mob_db2.txt > db/mob_db2.conf
#

use strict;
use warnings;

sub parse_mobdb (@) {
	my @input = @_;
	foreach (@input) {
		chomp $_;
#		ID,Sprite_Name,kROName,iROName,LV,HP,SP,EXP,JEXP,Range1,ATK1,ATK2,DEF,MDEF,STR,AGI,VIT,INT,DEX,LUK,Range2,Range3,Size,Race,Element,Mode,Speed,aDelay,aMotion,dMotion,MEXP,MVP1id,MVP1per,MVP2id,MVP2per,MVP3id,MVP3per,Drop1id,Drop1per,Drop2id,Drop2per,Drop3id,Drop3per,Drop4id,Drop4per,Drop5id,Drop5per,Drop6id,Drop6per,Drop7id,Drop7per,Drop8id,Drop8per,Drop9id,Drop9per,DropCardid,DropCardper
		if( $_ =~ qr/^
			(?<prefix>(?:\/\/[^0-9]*)?)
			(?<ID>[0-9]+)[^,]*,[\s\t]*
			(?<SpriteName>[a-zA-Z0-9_' ]+)[^,]*,[\s\t]*
			(?<kROName>[a-zA-Z0-9_' ]+)[^,]*,[\s\t]*
			(?<iROName>[a-zA-Z0-9_' ]+)[^,]*,[\s\t]*
			(?<LV>[0-9]+)[^,]*,[\s\t]*
			(?<HP>[0-9]+)[^,]*,[\s\t]*
			(?<SP>[0-9]+)[^,]*,[\s\t]*
			(?<EXP>[0-9]+)[^,]*,[\s\t]*
			(?<JEXP>[0-9]+)[^,]*,[\s\t]*
			(?<Range1>[0-9]+)[^,]*,[\s\t]*
			(?<ATK1>[0-9]+)[^,]*,[\s\t]*
			(?<ATK2>[0-9]+)[^,]*,[\s\t]*
			(?<DEF>[0-9]+)[^,]*,[\s\t]*
			(?<MDEF>[0-9]+)[^,]*,[\s\t]*
			(?<STR>[0-9]+)[^,]*,[\s\t]*
			(?<AGI>[0-9]+)[^,]*,[\s\t]*
			(?<VIT>[0-9]+)[^,]*,[\s\t]*
			(?<INT>[0-9]+)[^,]*,[\s\t]*
			(?<DEX>[0-9]+)[^,]*,[\s\t]*
			(?<LUK>[0-9]+)[^,]*,[\s\t]*
			(?<Range2>[0-9]+)[^,]*,[\s\t]*
			(?<Range3>[0-9]+)[^,]*,[\s\t]*
			(?<Size>[0-9]+)[^,]*,[\s\t]*
			(?<Race>[0-9]+)[^,]*,[\s\t]*
			(?<Element>[0-9]+)[^,]*,[\s\t]*
			(?<Mode>0[xX][A-Fa-f0-9]+)[^,]*,[\s\t]*
			(?<Speed>[0-9]+)[^,]*,[\s\t]*
			(?<aDelay>[0-9]+)[^,]*,[\s\t]*
			(?<aMotion>[0-9]+)[^,]*,[\s\t]*
			(?<dMotion>[0-9]+)[^,]*,[\s\t]*
			(?<MEXP>[0-9]+)[^,]*,[\s\t]*
			(?<MVPid1>[0-9]+)[^,]*,[\s\t]*
			(?<MVPper1>[0-9]+)[^,]*,[\s\t]*
			(?<MVPid2>[0-9]+)[^,]*,[\s\t]*
			(?<MVPper2>[0-9]+)[^,]*,[\s\t]*
			(?<MVPid3>[0-9]+)[^,]*,[\s\t]*
			(?<MVPper3>[0-9]+)[^,]*,[\s\t]*
			(?<Dropid1>[0-9]+)[^,]*,[\s\t]*
			(?<Dropper1>[0-9]+)[^,]*,[\s\t]*
			(?<Dropid2>[0-9]+)[^,]*,[\s\t]*
			(?<Dropper2>[0-9]+)[^,]*,[\s\t]*
			(?<Dropid3>[0-9]+)[^,]*,[\s\t]*
			(?<Dropper3>[0-9]+)[^,]*,[\s\t]*
			(?<Dropid4>[0-9]+)[^,]*,[\s\t]*
			(?<Dropper4>[0-9]+)[^,]*,[\s\t]*
			(?<Dropid5>[0-9]+)[^,]*,[\s\t]*
			(?<Dropper5>[0-9]+)[^,]*,[\s\t]*
			(?<Dropid6>[0-9]+)[^,]*,[\s\t]*
			(?<Dropper6>[0-9]+)[^,]*,[\s\t]*
			(?<Dropid7>[0-9]+)[^,]*,[\s\t]*
			(?<Dropper7>[0-9]+)[^,]*,[\s\t]*
			(?<Dropid8>[0-9]+)[^,]*,[\s\t]*
			(?<Dropper8>[0-9]+)[^,]*,[\s\t]*
			(?<Dropid9>[0-9]+)[^,]*,[\s\t]*
			(?<Dropper9>[0-9]+)[^,]*,[\s\t]*
			(?<DropCardid>[0-9]+)[^,]*,[\s\t]*
			(?<DropCardper>[0-9]+)[^,]*
		/x ) {
			my %cols = map { $_ => $+{$_} } keys %+;
			print "/*\n" if $cols{prefix};
			print "$cols{prefix}\n" if $cols{prefix} and $cols{prefix} !~ m|^//[\s\t]*$|;
			print "{\n";
			print "\tId: $cols{ID}\n";
			print "\tSpriteName: \"$cols{SpriteName}\"\n";
			print "\tName: \"$cols{kROName}\"\n";
			print "\tLevel: $cols{LV}\n";
			print "\tHP: $cols{HP}\n";
			print "\tSP: $cols{SP}\n";
			print "\tEXP: $cols{EXP}\n";
			print "\tJEXP: $cols{JEXP}\n";
			print "\tRange1: $cols{Range1}\n";
			print "\tATK1: $cols{ATK1}\n";
			print "\tATK2: $cols{ATK2}\n";
			print "\tDEF: $cols{DEF}\n";
			print "\tMDEF: $cols{MDEF}\n";
			print "\tStatus: {\n";
			print "\t\tSTR: $cols{STR}\n";
			print "\t\tAGI: $cols{AGI}\n";
			print "\t\tVIT: $cols{VIT}\n";
			print "\t\tINT: $cols{INT}\n";
			print "\t\tDEX: $cols{DEX}\n";
			print "\t\tLUK: $cols{LUK}\n";
			print "\t}\n";
			print "\tRange2: $cols{Range2}\n";
			print "\tRange3: $cols{Range3}\n";
			my $size = int($cols{Size});
			if( $size == 0 ) {
				print "\tSize: \"Size_Small\"\n";
			} elsif( $size == 1 ) {
				print "\tSize: \"Size_Medium\"\n";
			} elsif( $size == 2 ) {
				print "\tSize: \"Size_Large\"\n";
			}
			my $race = int($cols{Race});
			if( $race == 0 ) {
				print "\tRace: \"RC_Formless\"\n";
			} elsif( $race == 1 ) {
				print "\tRace: \"RC_Undead\"\n";
			} elsif( $race == 2 ) {
				print "\tRace: \"RC_Brute\"\n";
			} elsif( $race == 3 ) {
				print "\tRace: \"RC_Plant\"\n";
			} elsif( $race == 4 ) {
				print "\tRace: \"RC_Insect\"\n";
			} elsif( $race == 5 ) {
				print "\tRace: \"RC_Fish\"\n";
			} elsif( $race == 6 ) {
				print "\tRace: \"RC_Demon\"\n";
			} elsif( $race == 7 ) {
				print "\tRace: \"RC_DemiHuman\"\n";
			} elsif( $race == 8 ) {
				print "\tRace: \"RC_Angel\"\n";
			} elsif( $race == 9 ) {
				print "\tRace: \"RC_Dragon\"\n";
			}
			print "\tElement: {\n";
			my $element = int($cols{Element});
			my $etype = $element%20;
			if( $etype == 0 ) {
				print "\t\tType: \"Ele_Neutral\"\n";
			} elsif( $etype == 1 ) {
				print "\t\tType: \"Ele_Water\"\n";
			} elsif( $etype == 2 ) {
				print "\t\tType: \"Ele_Earth\"\n";
			} elsif( $etype == 3 ) {
				print "\t\tType: \"Ele_Fire\"\n";
			} elsif( $etype == 4 ) {
				print "\t\tType: \"Ele_Wind\"\n";
			} elsif( $etype == 5 ) {
				print "\t\tType: \"Ele_Poison\"\n";
			} elsif( $etype == 6 ) {
				print "\t\tType: \"Ele_Holy\"\n";
			} elsif( $etype == 7 ) {
				print "\t\tType: \"Ele_Dark\"\n";
			} elsif( $etype == 8 ) {
				print "\t\tType: \"Ele_Ghost\"\n";
			} elsif( $etype == 9 ) {
				print "\t\tType: \"Ele_Undead\"\n";
			}
			my $elevel = int($element/20);
			print "\t\tLevel: $elevel\n";
			print "\t}\n";
			print "\tMode: \"$cols{Mode}\"\n";
			print "\tSpeed: $cols{Speed}\n";
			print "\taDelay: $cols{aDelay}\n";
			print "\taMotion: $cols{aMotion}\n";
			print "\tdMotion: $cols{dMotion}\n";
			print "\tMEXP: $cols{MEXP}\n" if $cols{MEXP} > 0;
			print "\tMVPDrops: (\n" if $cols{MVPid1} || $cols{MVPid2} || $cols{MVPid3};
			print "\t{\n" if $cols{MVPid1};
			print "\t\tItemId: $cols{MVPid1}\n" if $cols{MVPid1};
			print "\t\tRate: $cols{MVPper1}\n" if $cols{MVPper1};
			print "\t},\n" if $cols{MVPid1} && ($cols{MVPid2} || $cols{MVPid3});
			print "\t{\n" if $cols{MVPid2};
			print "\t\tItemId: $cols{MVPid2}\n" if $cols{MVPid2};
			print "\t\tRate: $cols{MVPper2}\n" if $cols{MVPper2};
			print "\t},\n" if $cols{MVPid2} && $cols{MVPid3};
			print "\t{\n" if $cols{MVPid3};
			print "\t\tItemId: $cols{MVPid3}\n" if $cols{MVPid3};
			print "\t\tRate: $cols{MVPper3}\n" if $cols{MVPper3};
			print "\t}\n" if $cols{MVPid1} || $cols{MVPid2} || $cols{MVPid3};
			print "\t)\n" if $cols{MVPid1} || $cols{MVPid2} || $cols{MVPid3};
			print "\tDrops: (\n" if $cols{Dropid1} || $cols{Dropid2} || $cols{Dropid3} || $cols{Dropid4} || $cols{Dropid5} || $cols{Dropid6} || $cols{Dropid7} || $cols{Dropid8} || $cols{Dropid9} || $cols{DropCardid};
			print "\t{\n" if $cols{Dropid1};
			print "\t\tItemId: $cols{Dropid1}\n" if $cols{Dropid1};
			print "\t\tRate: $cols{Dropper1}\n" if $cols{Dropper1};
			print "\t},\n" if $cols{Dropid1} && ($cols{Dropid2} || $cols{Dropid3} || $cols{Dropid4} || $cols{Dropid5} || $cols{Dropid6} || $cols{Dropid7} || $cols{Dropid8} || $cols{Dropid9} || $cols{DropCardid});
			print "\t{\n" if $cols{Dropid2};
			print "\t\tItemId: $cols{Dropid2}\n" if $cols{Dropid2};
			print "\t\tRate: $cols{Dropper2}\n" if $cols{Dropper2};
			print "\t},\n" if $cols{Dropid2} && ($cols{Dropid3} || $cols{Dropid4} || $cols{Dropid5} || $cols{Dropid6} || $cols{Dropid7} || $cols{Dropid8} || $cols{Dropid9} || $cols{DropCardid});
			print "\t{\n" if $cols{Dropid3};
			print "\t\tItemId: $cols{Dropid3}\n" if $cols{Dropid3};
			print "\t\tRate: $cols{Dropper3}\n" if $cols{Dropper3};
			print "\t},\n" if $cols{Dropid3} && ($cols{Dropid4} || $cols{Dropid5} || $cols{Dropid6} || $cols{Dropid7} || $cols{Dropid8} || $cols{Dropid9} || $cols{DropCardid});
			print "\t{\n" if $cols{Dropid4};
			print "\t\tItemId: $cols{Dropid4}\n" if $cols{Dropid4};
			print "\t\tRate: $cols{Dropper4}\n" if $cols{Dropper4};
			print "\t},\n" if $cols{Dropid4} && ($cols{Dropid5} || $cols{Dropid6} || $cols{Dropid7} || $cols{Dropid8} || $cols{Dropid9} || $cols{DropCardid});
			print "\t{\n" if $cols{Dropid5};
			print "\t\tItemId: $cols{Dropid5}\n" if $cols{Dropid5};
			print "\t\tRate: $cols{Dropper5}\n" if $cols{Dropper5};
			print "\t},\n" if $cols{Dropid5} && ($cols{Dropid6} || $cols{Dropid7} || $cols{Dropid8} || $cols{Dropid9} || $cols{DropCardid});
			print "\t{\n" if $cols{Dropid6};
			print "\t\tItemId: $cols{Dropid6}\n" if $cols{Dropid6};
			print "\t\tRate: $cols{Dropper6}\n" if $cols{Dropper6};
			print "\t},\n" if $cols{Dropid6} && ($cols{Dropid7} || $cols{Dropid8} || $cols{Dropid9} || $cols{DropCardid});
			print "\t{\n" if $cols{Dropid7};
			print "\t\tItemId: $cols{Dropid7}\n" if $cols{Dropid7};
			print "\t\tRate: $cols{Dropper7}\n" if $cols{Dropper7};
			print "\t},\n" if $cols{Dropid7} && ($cols{Dropid8} || $cols{Dropid9} || $cols{DropCardid});
			print "\t{\n" if $cols{Dropid8};
			print "\t\tItemId: $cols{Dropid8}\n" if $cols{Dropid8};
			print "\t\tRate: $cols{Dropper8}\n" if $cols{Dropper8};
			print "\t},\n" if $cols{Dropid8} && ($cols{Dropid9} || $cols{DropCardid});
			print "\t{\n" if $cols{Dropid9};
			print "\t\tItemId: $cols{Dropid9}\n" if $cols{Dropid9};
			print "\t\tRate: $cols{Dropper9}\n" if $cols{Dropper9};
			print "\t},\n" if $cols{Dropid9} && $cols{DropCardid};
			print "\t{\n" if $cols{DropCardid};
			print "\t\tItemId: $cols{DropCardid}\n" if $cols{DropCardid};
			print "\t\tRate: $cols{DropCardper}\n" if $cols{DropCardper};
			print "\t}\n" if $cols{Dropid1} || $cols{Dropid2} || $cols{Dropid3} || $cols{Dropid4} || $cols{Dropid5} || $cols{Dropid6} || $cols{Dropid7} || $cols{Dropid8} || $cols{Dropid9} || $cols{DropCardid};
			print "\t)\n" if $cols{Dropid1} || $cols{Dropid2} || $cols{Dropid3} || $cols{Dropid4} || $cols{Dropid5} || $cols{Dropid6} || $cols{Dropid7} || $cols{Dropid8} || $cols{Dropid9} || $cols{DropCardid};
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
//= Mobs Database
//=========================================================================

/**************************************************************************
 ************* Entry structure ********************************************
 **************************************************************************
{
	Id: (int)
	SpriteName: (string)
	Name: (string)
	Level: (int)
	HP: (int)
	SP: (int)
	EXP: (int)
	JEXP: (int)
	Range1: (int)
	ATK1: (int)
	ATK2: (int)
	DEF: (int)
	MDEF: (int)
	Stats: {
		STR: (int)
		AGI: (int)
		VIT: (int)
		INT: (int)
		DEX: (int)
		LUK: (int)
	}
	Range2: (int)
	Range3: (int)
	Size: (string)
	Race: (string)
	Element: {
		Type: (string)
		Level: (int)
	}
	Mode: (string)
	Speed: (int)
	aDelay: (int)
	aMotion: (int)
	dMotion: (int)
	MEXP: (int)
	MVPDrops: ( (array)
	{
		ItemId: (int)
		Rate: (int)
		StealProtected: (bool)
	},
	... (can be repeated)
	)
	Drops: ( (array)
	{
		ItemId: (int)
		Rate: (int)
	},
	... (can be repeated)
	)
},
**************************************************************************/

mob_db: (
EOF

parse_mobdb(<>);

print ")\n";
