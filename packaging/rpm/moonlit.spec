%global gsr_version 5.12.5
%global gsr_component gpu-screen-recorder

Name:           moonlit
Version:        0.1.0
Release:        1%{?dist}
Summary:        Windows-first local game clip recorder
License:        GPL-3.0-only
URL:            https://github.com/SourisCG/MoonLit
ExclusiveArch:  x86_64

# These are release artifacts staged by CI. They are intentionally not stored
# in Git and must be produced from the pinned sources described in docs/PACKAGING.md.
Source0:        %{name}-%{version}-x86_64.tar.gz
Source1:        %{gsr_component}-%{gsr_version}-x86_64.tar.gz

Requires:       webkit2gtk4.1
Requires:       pipewire
Requires:       xdg-desktop-portal
Requires:       libcap
Requires:       libdrm
Requires:       libva

%description
MoonLit is a Windows-first game clip recorder. This package includes a
pinned gpu-screen-recorder component while retaining FakeBackend support when
the host GPU, driver or desktop portal is unavailable.

%prep
%setup -q -n %{name}-%{version} -a 1

%build
# The application and GSR artifacts are built before rpmbuild by the release
# pipeline. This spec packages only verified staged output.

%install
rm -rf %{buildroot}

install -Dpm0755 moonlit %{buildroot}%{_bindir}/moonlit
install -Dpm0755 %{gsr_component}/gpu-screen-recorder \
    %{buildroot}%{_libexecdir}/moonlit/gpu-screen-recorder
install -Dpm0755 %{gsr_component}/gsr-kms-server \
    %{buildroot}%{_libexecdir}/moonlit/gsr-kms-server
install -Dpm0644 %{gsr_component}/components.json \
    %{buildroot}%{_datadir}/moonlit/components.json
install -Dpm0644 LICENSE \
    %{buildroot}%{_datadir}/licenses/%{name}/LICENSE
install -Dpm0644 %{gsr_component}/LICENSE \
    %{buildroot}%{_datadir}/licenses/%{name}/GSR-LICENSE

%post
if [ -x %{_libexecdir}/moonlit/gsr-kms-server ] && command -v setcap >/dev/null 2>&1; then
    setcap cap_sys_admin+ep %{_libexecdir}/moonlit/gsr-kms-server || :
fi

%postun
if [ "$1" -eq 0 ] && command -v setcap >/dev/null 2>&1; then
    setcap -r %{_libexecdir}/moonlit/gsr-kms-server 2>/dev/null || :
fi

%files
%{_bindir}/moonlit
%dir %{_libexecdir}/moonlit
%attr(0755,root,root) %{_libexecdir}/moonlit/gpu-screen-recorder
%attr(0755,root,root) %{_libexecdir}/moonlit/gsr-kms-server
%{_datadir}/moonlit/components.json
%license %{_datadir}/licenses/%{name}/LICENSE
%license %{_datadir}/licenses/%{name}/GSR-LICENSE

%changelog
* Thu Jul 23 2026 SourisCG - 0.1.0-1
- Add the first Fedora x86_64 packaging contract with bundled GSR.
