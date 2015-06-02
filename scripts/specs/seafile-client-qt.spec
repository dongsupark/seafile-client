Name:           seafile-client-qt
Version:        4.2.2
Release:        1%{?dist}
Summary:        Cloud storage system (Qt graphical client)

%global _hardened_build 1
%global sourcename seafile-client

License:        ASL 2.0
URL:            https://github.com/haiwen/%{sourcename}
Source0:        https://github.com/haiwen/%{sourcename}/archive/v%{version}.tar.gz

BuildRequires:  desktop-file-utils
BuildRequires:  cmake >= 2.6
BuildRequires:  qt5-qtbase-devel
BuildRequires:  qt5-qttools-devel
BuildRequires:  sqlite-devel
BuildRequires:  jansson-devel >= 2.0
BuildRequires:  ccnet-devel >= 1.4.2-6
BuildRequires:  libsearpc-devel >= 1.0
BuildRequires:  openssl-devel >= 0.98
BuildRequires:  seafile-devel >= 1.7

%description
Seafile is a next-generation open source cloud storage system, with
advanced support for file syncing, privacy protection and teamwork

%prep
%setup -q -n %{sourcename}-%{version}

%build
%cmake -D CMAKE_BUILD_TYPE=Release -D USE_QT5=OFF .
make %{?_smp_mflags}

%install
make install DESTDIR=%{buildroot}

%files
%doc LICENSE README.md
%{_bindir}/*
%{_datadir}/applications/*.desktop
%{_datadir}/icons/hicolor/*/apps/*
%{_datadir}/pixmaps/*

%changelog
* Wed May 27 2015 Dongsu Park <hipporoll@posteo.de> - 4.2.2-2
- turn off USE_QT5 to avoid crashes with qt5
* Tue May 26 2015 Philipp Kerling <pkerling@casix.org> - 4.2.2-1
- updated for seafile 4.2.2
- enable hardened build
* Tue May 19 2015 Philipp Kerling <pkerling@casix.org> - 4.2.1-1
- updated for seafile 4.2.1
* Fri Apr 17 2015 Philipp Kerling <pkerling@casix.org> - 4.1.5-1
- updated for seafile 4.1.5
* Sun Mar 22 2015 Philipp Kerling <pkerling@casix.org> - 4.1.2-1
- updated for seafile 4.1.2
* Sun Mar 08 2015 Philipp Kerling <pkerling@casix.org> - 4.1.1-1
- updated for seafile 4.1.1
* Thu Dec 25 2014 Philipp Kerling <pkerling@casix.org> - 4.0.5-1
- updated for seafile 4.0.5
* Fri Dec 19 2014 Philipp Kerling <pkerling@casix.org> - 4.0.4-1
- updated for seafile 4.0.4
* Thu Nov 13 2014 Philipp Kerling <pkerling@casix.org> - 3.1.10-1
- updated for seafile 3.1.10
* Tue Nov 11 2014 Philipp Kerling <pkerling@casix.org> - 3.1.8-1
- updated for seafile 3.1.8
* Sun Sep 28 2014 Philipp Kerling <pkerling@casix.org> - 3.1.7-1
- updated for seafile 3.1.7
* Fri Sep 19 2014 Philipp Kerling <pkerling@casix.org> - 3.1.6-1
- updated for seafile 3.1.6
* Fri Aug 22 2014 Philipp Kerling <pkerling@casix.org> - 3.1.5-1
- updated for seafile 3.1.5
* Thu Aug 07 2014 Philipp Kerling <pkerling@casix.org> - 3.1.4-1
- updated for seafile 3.1.4
* Sat May 17 2014 Philipp Kerling <pkerling@casix.org> - 3.0.4-1
- updated for seafile 3.0.4
* Mon Apr 28 2014 Philipp Kerling <pkerling@casix.org> - 3.0.2-1
- updated for seafile 3.0.2
* Sat Apr 19 2014 Philipp Kerling <pkerling@casix.org> - 2.2.0-1
- updated for seafile 2.2.0
* Mon Mar 10 2014 Philipp Kerling <pkerling@casix.org> - 2.1.2-1
- updated for seafile 2.1.2
* Sat Nov 16 2013 Philipp Kerling <pkerling@casix.org> - 2.0.8-1
- Initial package
