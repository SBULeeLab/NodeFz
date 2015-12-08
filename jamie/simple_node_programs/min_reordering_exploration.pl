#!/usr/bin/env perl
#Author: Jamie Davis <davisjam@vt.edu>
#Description: This is a test program for my Node/libuv work done reordering implementation.
# It explores a variety of function invocation orderings, and should print
# application-specific lines in the same order as an invocation of the min_reordering.js program.

if (@ARGV != 3)
{
  print "Description: Test program for Node/libuv reordering
Usage: $0 reorder_distance min_reorder_distance_to_fail n_functions
";
  exit 1;
}

my $glob = 0;
&main (@ARGV);

#########################################

sub main
{
  my $reorder_distance = shift @ARGV;
  my $min_reordering = shift @ARGV;
  my $n_functions = shift @ARGV;

  #input: ($i) -- function number
  #output: $f: a function
  my $factory = sub {
    my ($i) = @_;
    return sub {
      my $before = $glob;
      $glob++;
      print "APP: incrementer_$i: global_counter BEFORE $before AFTER $glob\n";
    };
  };

  my $final = sub {
    if ($glob + 1 < $min_reordering)
    {
      print "APP: reorder_test: global_counter $glob + 1 < min_distance_required $min_reordering. min_distance_required not met.\n"; 
    }
    else
    {
      print "APP: reorder_test: min_distance_required $min_reordering <= global_counter $glob. min_distance_required met.\n";
    }
  };

  my @incrementers = map{ $factory->($_) } (0 .. $n_functions-1);
  my @functions = ($final, @incrementers);

  &reorder (\@functions, $reorder_distance);
  exit 0;
}

#input: (\@funcs, $reorder_distance)
#output: ()
#executes @funcs in groups of reorder_distance:
# - forward and recurse
# - backward and recurse
sub reorder 
{
  my ($funcs, $reorder_distance) = @_;
  my @funcs = @$funcs;

  my $glob_orig = $glob;

  #no functions left
  if (@funcs == 0)
  {
    return;
  }

  #execute the first $reorder_distance forwards and recurse, then backwards and recurse
  my @funcs_to_execute;
  if (@funcs < $reorder_distance)
  {
    @funcs_to_execute = @funcs;
    @funcs = ();
  }
  else
  {
    @funcs_to_execute = @funcs[0 .. $reorder_distance-1];
    shift @funcs for (1 .. $reorder_distance);
  }

  #forwards and recurse
  $glob = $glob_orig;
  for (my $i = 0; $i < @funcs_to_execute; $i++)
  {
    $funcs_to_execute[$i]->();
  }
  &reorder (\@funcs, $reorder_distance);

  #backwards and recurse
  $glob = $glob_orig;
  for (my $i = @funcs_to_execute-1; $i >= 0; $i--)
  {
    $funcs_to_execute[$i]->();
  }
  &reorder (\@funcs, $reorder_distance);
}
