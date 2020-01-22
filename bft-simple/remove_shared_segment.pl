#!/usr/bin/env perl

# permissions
$permissionShmem=666;
$permissionMQ=600;

# username?
chomp($username = `whoami`);

#print("Username: ".$username."\n");
#print("Permission: ".$permissionShmem."\n");

open(IPCS,"ipcs -m |") || die "Failed: $!\n";
while ( <IPCS> )
{
   #print($_);

   if ($_ =~ m/^0x[\da-fA-F]+\s+(\d+)\s+$username\s+$permissionShmem/) {
      print("Killing $1\n");
      system("ipcrm -m $1");
   }
}
close(IPCS);

open(IPCS,"ipcs -q |") || die "Failed: $!\n";
while ( <IPCS> )
{
   #print($_);

   if ($_ =~ m/^0x[\da-fA-F]+\s+(\d+)\s+$username\s+$permissionMQ/) {
      print("Killing $1\n");
      system("ipcrm -q $1");
   }
}
close(IPCS);
