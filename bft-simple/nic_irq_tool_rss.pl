#!/usr/bin/perl -w
# Authors:
# Fabien Gaud (fabien.gaud@inria.fr)
# Baptiste Lepers (baptiste.lepers@inria.fr)

use strict;
use warnings;
use Getopt::Long;
use Data::Dumper;

my $nb_cpus = get_nb_cpu();
my $irq_file = "/proc/interrupts";
my %eths;


#my %rrbft_sci74 = ("eth8" => "7", "eth1" => "0", "eth4" => "3", "eth6" => "4", "eth7" => "5", "eth2" => "6");
#my %rrbft_sci75 = ("eth0" => "7", "eth2" => "0", "eth7" => "3", "eth3" => "4", "eth4" => "5", "eth5" => "6");
#my %rrbft_sci76 = ("eth0" => "7", "eth2" => "0", "eth3" => "3", "eth4" => "4", "eth6" => "5", "eth9" => "6");
#my %rrbft_sci77 = ("eth0" => "7", "eth2" => "0", "eth3" => "3", "eth5" => "4", "eth6" => "5", "eth9" => "6");

my %rrbft_sci74 = ("eth8" => "7", "eth1" => "0", "eth6" => "4", "eth7" => "5", "eth2" => "6");
my %rrbft_sci75 = ("eth0" => "7", "eth2" => "0", "eth3" => "4", "eth4" => "5", "eth5" => "6");
my %rrbft_sci76 = ("eth0" => "7", "eth2" => "0", "eth4" => "4", "eth6" => "5", "eth9" => "6");
my %rrbft_sci77 = ("eth0" => "7", "eth2" => "0", "eth5" => "4", "eth6" => "5", "eth9" => "6");

my %irq_map = (
"rrbft_sci74" => \%rrbft_sci74,
"rrbft_sci75" => \%rrbft_sci75,
"rrbft_sci76" => \%rrbft_sci76,
"rrbft_sci77" => \%rrbft_sci77
);


my ($help, $irq_aff_name);
usage() if ( ! GetOptions('help|h' => \$help, 's=s' => \$irq_aff_name) or defined $help );

sub usage {
	print "Unknown option: @_\n" if ( @_ );
	print "usage: program [-s irq_aff_name] [-l] [-h]\n";
	list_aff ();
	exit;
}

sub get_nb_cpu{
	if(!defined $nb_cpus){
		my $nb_online_cpu = 1;
		my $cpu_file_name = "/sys/devices/system/cpu/cpu1/online";
		my $current_cpu = 1;
		while(-e $cpu_file_name) {
			my $online = `cat $cpu_file_name`;
			$nb_online_cpu++;
			$current_cpu++;
			$cpu_file_name =  "/sys/devices/system/cpu/cpu".$current_cpu."/online";
		}
		$nb_cpus = $nb_online_cpu;
	}

	return $nb_cpus;
}

sub get_closest_proc {
	my $found = 0;
	my $current_cpu = $_[0];
	my $cpu_file_name = "/sys/devices/system/cpu/cpu${current_cpu}/online";
	while($found == 0 && $current_cpu > 0) {
		my $online = `cat $cpu_file_name`;
		if($online == 1) {
			$found = 1;
		} else {
			$current_cpu--;
			$cpu_file_name =  "/sys/devices/system/cpu/cpu".$current_cpu."/online";
		}
	}
	return $current_cpu;
}

sub get_next_proc {
	my $found = 0;
	my $current_cpu = ($_[0]+1)%get_nb_cpu();
	my $cpu_file_name = "/sys/devices/system/cpu/cpu${current_cpu}/online";
	while($found == 0 && $current_cpu != 0) {
		my $online = `cat $cpu_file_name`;
		if($online == 1) {
			$found = 1;
		} else {
			$current_cpu = ($current_cpu+1)%get_nb_cpu();
			$cpu_file_name =  "/sys/devices/system/cpu/cpu".$current_cpu."/online";
		}
	}
	return $current_cpu;
}

sub bin2dec {
	my $bin = $_[0];
	my $dec = 0;
	while ($bin != 1){
		$dec++;
		$bin = $bin >> 1;

		if($bin == 0){
			printf "Error. Cannot convert binary to value (binary was %32b)", $_[0];
			exit(-1);
		}
	}

	return $dec;
}

sub eth_driver {
	my ($eth_name) = @_;
	my $eth_out = `sudo ethtool -i $eth_name 2>&1`;     
	if($? != -1){
		my ($driver) =  ($eth_out =~ m/driver:\s+(\w+)/);  
		if(!$driver) {
			die "Cannot find driver for $eth_name : $eth_out\n";
		} else {
			return $driver;
		}
	} 
}

sub eth_status {
	my ($eth_name) = @_;
	my $link_status = "";
	my $eth_out = `sudo ethtool $eth_name 2>&1`;
	if($? != -1){
		($link_status) =  ($eth_out =~ m/Link detected:\s+(\w+)/);   
	} 
	return $link_status;
}

sub eth_irq {
	my ($eth_name) = @_;
	my $irq_no = $eths{$eth_name};
	my $irq_aff = `echo \"cat /proc/irq/$irq_no/smp_affinity\" | sudo -s`;
	chop($irq_aff);
	return $irq_aff;
}

sub parse_irq_eth {
	open(FILE,"cat $irq_file|grep eth|") || die "Can't open $irq_file\n";
	my @lines = <FILE>;
	close(FILE);

	my $current_cpu = 0;
	for my $line (@lines) {
		next if($line !~ /(\d+):.*eth(\d+)/);

		my $current_irq = $1;
		my $current_eth = $2;

		if($line =~ m/(rx|tx|TxRx)-(\d+)/) {
			$eths{$current_eth} = -1;
			$eths{$current_eth."-".$1."-".$2} = $current_irq;
		} else {
			next if(defined($eths{$current_eth}));
			$eths{$current_eth} = $current_irq;
		}
	}
}

sub show_irqs {
	print "\nCurrent IRQ configuration :\n";
	printf "        %10s\t%10s\t%15s\t%10s\t%15s\n","ETH NAME","IRQ_NO", "IRQ_AFFINITY", "DRIVER", "LINK DETECTED";

	foreach my $eth_num ( sort { (($a) =~ m/(\d+)/)[0] <=> (($b) =~ m/(\d+)/)[0] } keys %eths) {
		next if($eth_num =~ m/(rx|tx|TxRx)/);

		if($eths{$eth_num} == -1) {
			printf "%8s\n", 'eth'.$eth_num;
			my $i = 0;
			while(defined($eths{$eth_num.'-rx-'.$i})) {
				print "        ";
				show_irq($eth_num.'-rx-'.$i, 'eth'.$eth_num);
				$i++;
			}
			while(defined($eths{$eth_num.'-TxRx-'.$i})) {
				print "        ";
				show_irq($eth_num.'-TxRx-'.$i, 'eth'.$eth_num);
				$i++;
			}
		} else {
			print "        ";
			show_irq($eth_num, 'eth'.$eth_num);
		}
	}
}
sub show_irq {
	my ($eth_num, $eth_name) = @_;
	if(eth_irq($eth_num) =~ m/ff/){
		printf "%10s\t%10d\t%15s\t%10s\t%15s\n", 'eth'.$eth_num, $eths{$eth_num}, sprintf("%7s (%x)", "bcast", hex(eth_irq($eth_num))), eth_driver($eth_name), eth_status($eth_name);
	} else {
		my $dec_value = bin2dec(hex(eth_irq($eth_num)));
		printf "%10s\t%10d\t%15s\t%10s\t%15s\n", 'eth'.$eth_num, $eths{$eth_num}, sprintf("%7d (%x)", $dec_value, hex(eth_irq($eth_num))), eth_driver($eth_name), eth_status($eth_name);
	}
}

sub set_irqs_from_map {
	print "Setting irq configuration $irq_aff_name\n";

	my $map_ptr = $irq_map{$irq_aff_name};
	my %map = %$map_ptr;

	foreach my $eth_num ( sort { (($a) =~ m/(\d+)/)[0] <=> (($b) =~ m/(\d+)/)[0] } keys %eths) {

		next if($eth_num =~ m/(rx|tx|TxRx)/);

		my $eth_name = "eth".$eth_num;
		my $current_cpu = $nb_cpus-1;
		if (defined($map{$eth_name})) {
			$current_cpu = $map{$eth_name};
		}
		print "$eth_name on core $current_cpu\n";

		if($eths{$eth_num} == -1) {
			my $i = 0;
			while(defined($eths{$eth_num.'-rx-'.$i})) {
				set_irq($eth_num.'-rx-'.$i, 'eth'.$eth_num, $current_cpu);
				set_irq($eth_num.'-tx-'.$i, 'eth'.$eth_num, $current_cpu);
				$i++;
				$current_cpu = get_next_proc($current_cpu) if($irq_aff_name eq "rss");
			}
			while(defined($eths{$eth_num.'-TxRx-'.$i})) {
				set_irq($eth_num.'-TxRx-'.$i, 'eth'.$eth_num, $current_cpu);
				$i++;
				$current_cpu = get_next_proc($current_cpu) if($irq_aff_name eq "rss");
			}
			$current_cpu = get_next_proc($current_cpu) if($irq_aff_name eq "one_per_core");
		} else {
			set_irq($eth_num, 'eth'.$eth_num, $current_cpu);
			$current_cpu = get_next_proc($current_cpu);
		}
	}
}


sub set_irqs {
	print "Setting irq configuration $irq_aff_name\n";
	die if($irq_aff_name ne "one_per_core" && $irq_aff_name ne "rss");

	my $current_cpu = 0;
	foreach my $eth_num ( sort { (($a) =~ m/(\d+)/)[0] <=> (($b) =~ m/(\d+)/)[0] } keys %eths) {


		next if($eth_num =~ m/(rx|tx|TxRx)/);

		if($eths{$eth_num} == -1) {
			my $i = 0;
			while(defined($eths{$eth_num.'-rx-'.$i})) {
				set_irq($eth_num.'-rx-'.$i, 'eth'.$eth_num, $current_cpu);
				set_irq($eth_num.'-tx-'.$i, 'eth'.$eth_num, $current_cpu);
				$i++;
				$current_cpu = get_next_proc($current_cpu) if($irq_aff_name eq "rss");
			}
			while(defined($eths{$eth_num.'-TxRx-'.$i})) {
				set_irq($eth_num.'-TxRx-'.$i, 'eth'.$eth_num, $current_cpu);
				$i++;
				$current_cpu = get_next_proc($current_cpu) if($irq_aff_name eq "rss");
			}
			$current_cpu = get_next_proc($current_cpu) if($irq_aff_name eq "one_per_core");
		} else {
			set_irq($eth_num, 'eth'.$eth_num, $current_cpu);
			$current_cpu = get_next_proc($current_cpu);
		}
	}
}

sub set_irq {
	my ($eth_num, $eth_name, $proc) = @_;
	my $eth_driver = eth_driver($eth_name);

	my $core_hexa = 1 << get_closest_proc($proc);
	my $core_hexa_str= sprintf("%x",$core_hexa);
	my $irq_no = $eths{$eth_num};
	printf "eth$eth_num ($eth_driver) -> core %d (%x)\n", get_closest_proc($proc), $core_hexa;

	if($eth_driver =~ /bnx/) {
		my $old_default = `echo \"cat /proc/irq/default_smp_affinity\" | sudo -s`;
		chop($old_default);
		system "echo \"echo $core_hexa_str > /proc/irq/default_smp_affinity\" | sudo -s";
		system "echo \"ifconfig $eth_name down\" | sudo -s";
		system "echo \"ifconfig $eth_name up\" | sudo -s";
		system "echo \"echo $old_default > /proc/irq/default_smp_affinity\" | sudo -s";
		system "echo \"echo $core_hexa_str > /proc/irq/$irq_no/smp_affinity\" | sudo -s";
	} elsif($eth_driver =~ /igb/) {
		system "echo \"echo $core_hexa_str > /proc/irq/$irq_no/smp_affinity\" | sudo -s";
	} else {
		system "echo \"echo $core_hexa_str > /proc/irq/$irq_no/smp_affinity\" | sudo -s";
	}
}

parse_irq_eth();

if($irq_aff_name){
    if (defined($irq_map{$irq_aff_name})) {
      set_irqs_from_map();
    } else {
      set_irqs();
    }
}

show_irqs();
