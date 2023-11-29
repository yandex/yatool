SCRIPT = r"""#!/usr/bin/perl -w

=head1 NAME

dh_noconffiles - Remove unnecessary records from conffiles. Use after invoking dh_installdeb

=cut

use strict;
use Debian::Debhelper::Dh_Lib;

=head1 SYNOPSIS

B<dh_noconffiles> [S<I<debhelper options>>]

=head1 DESCRIPTION

B<dh_noconffiles> is a debhelper program that is responsible for cleaning
unnecessary records from conffiles. This might be needed since L<dh_installdeb(1)>
marks every file in /etc/ as conffile leading to sometimes undesirable L<dpkg(1)>
behaviour when these files are somehow touched on-site.
Surely it is also possible to avoid such flagging by setting compatible level to 2,
but it triggers lintian warnings. More important, it also changes many other things.

B<NOTE:> As conffiles list is filled by L<dh_installdeb(1)> this debhelper
will only have an effect when launched after L<dh_installdeb(1)>.

=head1 FILES

=over 4

=item I<package>.noconffiles

List files which are to be removed from conffiles. The format is a set of
lines, where each line lists a file (or files) to be unflagged. The name of
the files should be given the same way they are listed in conffiles.
Additionally you may use wildcards at the end of names of the files.
Nothing is done if this file is absent and there is no B<--all> option.

=back

=head1 OPTIONS

=over 4

=item B<-A> B<--all>

Remove all conffiles flags regardless of I<package>.noconffiles content.

=back

=cut

init();

foreach my $package (@{$dh{DOPACKAGES}}) {

    my $tmp = tmpdir($package);

    if (-d "$tmp/DEBIAN") {
        my $conffilesfile = "$tmp/DEBIAN/conffiles";
        if ($dh{PARAMS_ALL}) {
            doit("rm", "-f", $conffilesfile);
        } else {
            my $file = pkgfile($package,"noconffiles");
            if ($file) {
                my @noconffiles = filearray($file);
                my @conffiles = filearray($conffilesfile);
                foreach my $noconf (@noconffiles) {
                    error("Whildcards are only allowed at the end of file name") if $noconf =~ /\*./;
                    if ($noconf =~ /^(.*)\*$/) {
                        my $prefix = $1;
                        @conffiles = grep {substr($_, 0, length($prefix)) ne $prefix } @conffiles;
                    } else {
                        @conffiles = grep {$_ ne $noconf} @conffiles;
                    }
                }
                if (@conffiles) {
                    verbose_print("Writing modified conffiles...");
                    open (my $FH, '>', $conffilesfile) or error("open $conffilesfile failed: $!");
                    print $FH join("\n", @conffiles) . "\n";
                    close($FH);
                    doit("chmod", 644, $conffilesfile);
                } else {
                    doit("rm", "-f", $conffilesfile);
                }
            }
        }

    } else {
        error("There's no DEBIAN directory in $package build directory. Keep in mind that dh_noconffiles should be run after dh_installdeb");
    }

}

=head1 SEE ALSO

L<debhelper(7)>

=head1 AUTHOR

Roman Andriadi <nARN@yandex-team.ru>

=cut
"""
