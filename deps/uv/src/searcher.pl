#!/usr/bin/env perl
#Author: Jamie Davis
#Description: Find CBs that aren't being handled through the unified callback mechanism

my @files = @ARGV;
if (not @files)
{
  print "Find CBs that aren't being handled through the unified callback mechanism
Usage: $0 f1 f2 ...
";
  exit 0;
}

my %failed_files;
for my $f (@files)
{
  #print "Checking file $f\n";
  if (not -f $f)
  {
    print "Error, $f is not a file\n";
    next;
  }
  my @lines = `cat $f`; chomp @lines;
  my $should_be_hashEnd = 0;
  my $prev_line;
  for my $l (@lines)
  {
    if ($should_be_hashEnd)
    {
      if ($l !~ m/\s*#end/)
      {
        print "$f: FAIL: Line <$l> after <$prev_line> should be #end\n";
        $failed_files{$f}++;
      }
      $should_be_hashEnd = 0;
      next;
    }
    if ($l =~ m/cb\s*\(/ and ($l !~ m/^void/ and $l !~ m/^static/))
    {
      $prev_line = $l;
      $should_be_hashEnd = 1;
    }
  }
}

my @keys = keys %failed_files;
if (@keys)
{
  print "Failed files:\n";
  for my $k (@keys)
  {
    print "  $k\n";
  }
}
else
{
  print "All " . scalar(@files) . " files passed\n";
}

exit 0;
