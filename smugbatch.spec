# norootforbuild


Name:           smugbatch
Group:		Productivity/Graphics/Other
%if 0%{suse_version} > 1020
BuildRequires:  libcurl-devel
%else
BuildRequires:  curl-devel
%endif
License:        GPL v2 only
Version:	002
Release:	1
AutoReqProv:    on
Summary:	Tools for smugmug.com
Source:		smugbatch-%{version}.tar.bz2
Url:            http://www.kernel.org/pub/linux/kernel/people/gregkh/smugbatch/
BuildRoot:      %{_tmppath}/%{name}-%{version}-build

%description
Command line tools to access smugmug.com.


Authors:
--------
    Greg Kroah-Hartman <greg@kroah.org>

%prep
%setup -q

%build
make V=1 CFLAGS="$RPM_OPT_FLAGS"

%install
mkdir -p $RPM_BUILD_ROOT/usr/bin
install smugup smugls $RPM_BUILD_ROOT/usr/bin

%files
%defattr(-,root,root)
%doc RELEASE-NOTES COPYING ChangeLog
/usr/bin/smug*

%changelog
* Thu May 01 2008 aj@suse.de
- Initial build version.

