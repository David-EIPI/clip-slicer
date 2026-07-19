# SPDX-FileCopyrightText: 2026 David Shirvanyants
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

%global debug_package %{nil}
%global soversion 0

Name:           clip-slicer
Version:        0.1.0
Release:        2%{?dist}
Summary:        STL slicing and support-generation applications

License:        PolyForm-Noncommercial-1.0.0 AND CC0-1.0
URL:            https://github.com/David-EIPI/clip-slicer
Source0:        %{url}/archive/refs/tags/v%{version}.tar.gz

BuildRequires:  cmake
BuildRequires:  gcc-c++
BuildRequires:  ninja-build
BuildRequires:  polyclipping2-devel >= 2.0.1
BuildRequires:  wxGTK-devel >= 3.2
BuildRequires:  gtk3-devel
BuildRequires:  libepoxy-devel
BuildRequires:  mesa-libGLU-devel

Requires:       %{name}-libs%{?_isa} = %{version}-%{release}

%description
CLIP Slicer provides a wxWidgets desktop application for inspecting,
transforming, slicing, and generating supports for STL models. This package
also contains the stl-slicer command-line batch slicing tool.

%package libs
Summary:        Shared slicing library for CLIP Slicer
License:        PolyForm-Noncommercial-1.0.0

%description libs
This package contains the shared C++ library used by the CLIP Slicer desktop
and command-line applications.

%prep
%autosetup -n %{name}-%{version}

%build
%cmake \
    -DSTL_SLICER_BUILD_GUI=ON \
    -DSTL_SLICER_BUILD_TESTS=ON
%cmake_build

%install
%cmake_install
cp -a %{buildroot}%{_licensedir}/%{name} \
    %{buildroot}%{_licensedir}/%{name}-libs
install -d %{buildroot}%{_docdir}/%{name}
install -m 0644 README.md %{buildroot}%{_docdir}/%{name}/README.md
sed -i '/<!-- RPM-EXCLUDE-BEGIN:/,/<!-- RPM-EXCLUDE-END -->/d' \
    %{buildroot}%{_docdir}/%{name}/README.md

%check
%ctest

%files
%license %{_licensedir}/%{name}/LICENSE.md
%license %{_licensedir}/%{name}/LICENSES/*
%doc %{_docdir}/%{name}/README.md
%{_bindir}/clip-slicer
%{_bindir}/stl-slicer

%files libs
%license %{_licensedir}/%{name}-libs/LICENSE.md
%license %{_licensedir}/%{name}-libs/LICENSES/*
%{_libdir}/libstl_slicer.so
%{_libdir}/libstl_slicer.so.%{soversion}
%{_libdir}/libstl_slicer.so.%{version}

%changelog
* Sun Jul 19 2026 CLIP Slicer contributors <noreply@example.com> - 0.1.0-2
- Declare source, documentation, artwork, and screenshot licenses.
- Omit repository screenshots and their references from the binary RPM.

* Sun Jul 19 2026 CLIP Slicer contributors <noreply@example.com> - 0.1.0-1
- Add the initial RPM package split for the shared library and applications.
