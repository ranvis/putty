use v5.30;
use warnings;

binmode(STDOUT);

my ($fp, %chrs);
open($fp, '<', 'EastAsianWidth.txt') or die $!;
my ($version) = ($_ = <$fp>) =~ /^# \w+-(.+?)\.txt$/ or die;
say get_header($version);

while (<$fp>) {
	my @fields = split_fields($_);
	next if (substr($fields[1] // '', 0, 1) ne 'A');
	my ($chr, $end) = map(hex, split(/\.\./, $fields[0]));
	$end //= $chr;
	while ($chr <= $end) {
		$chrs{$chr++} = 1;
	}
}
# exclude combining[]
open($fp, '<', 'UnicodeData.txt') or die $!;
while (<$fp>) {
	my @fields = split_fields($_);
	my $chr = hex $fields[0];
	my $cat = $fields[2];
	my $include = ($cat eq "Me" || $cat eq "Mn" || $cat eq "Cf");
	$include = 0 if ($chr == 0x00AD);
	$include = 1 if (0x1160 <= $chr && $chr <= 0x11FF);
	$include = 1 if ($chr == 0x200B);
	delete $chrs{$chr} if ($include);
}
close($fp);
for (my $chr = 0; $chr < 0x110000; $chr++) {
	if ($chrs{$chr}) {
		my $start = $chr;
		$chr++ while $chrs{$chr};
		printf "{0x%04x, 0x%04x},\n", $start, $chr-1;
	}
}

sub get_header {
	my ($version) = @_;
	local $/ = undef;
	my $header = <DATA>;
	$header =~ s/<PROGRAM>/$0/;
	$header =~ s/<VERSION>/$version/;
	return $header;
}

sub split_fields {
	my ($data) = @_;
	return map {
		$_ =~ s/^ +| +$//gr
	} split(/;/, $data);
}

__DATA__
/*
 * Autogenerated by <PROGRAM> from The Unicode Standard <VERSION>
 *
 * Identify Unicode characters that are width-ambiguous: some regimes
 * regard them as occupying two adjacent character cells in a terminal,
 * and others do not.
 *
 * Used by utils/wcwidth.c.
 */
