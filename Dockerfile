FROM ubuntu:22.04 AS base

RUN set -ex \
	&& apt update \
	&& apt install -y --no-install-recommends \
		ca-certificates	\
		python3 \
		python3-pip \
		python-is-python3 \
		libpython3.10 \
	&& rm -rf /var/lib/apt/lists/*

RUN set -ex \
	&& pip install cython \
	&& cython --version


FROM base as builder

SHELL ["/bin/bash", "-c"]
WORKDIR /builds

# Install build dependencies
RUN set -exo pipefail \
	&& apt update \
	&& apt install -y --no-install-recommends \
		git \
		autoconf \
		automake \
		libtool \
		build-essential \
		checkinstall \
		pkg-config \
		python3-dev \
	&& rm -rf /var/lib/apt/lists/*

# Build & Install zimg
RUN set -exo pipefail \
	&& git clone https://github.com/sekrit-twc/zimg \
		--depth 1 \
		--branch release-3.0.4 \
		--shallow-submodules \
		--recurse-submodules \
	&& cd zimg \
	&& ./autogen.sh \
	&& ./configure --prefix=/usr \
	&& make -j$(nproc) \
	&& checkinstall --fstrans=no --default -D \
		--pkgversion=3.0.4 \
		--pkgrelease=0

# Build & Install VapourSynth
COPY . vapoursynth
RUN set -exo pipefail \
	&& cd vapoursynth \
	&& ./autogen.sh \
	&& ./configure --prefix=/usr \
	&& make -j$(nproc) \
	&& checkinstall --fstrans=no --default -D \
		--pkgversion=$(python setup.py --version) \
		--pkgrelease=0 \
	&& python setup.py bdist_wheel -d bdist_wheel \
	&& mkdir empty && pushd empty \
	&& pip install --no-index ../bdist_wheel/*.whl \
	&& popd \
	&& python -m unittest discover -s test -p "*test.py"


FROM base as runtime

# Install zimg
COPY --from=builder /builds/zimg/zimg*.deb /tmp/zimg.deb
RUN apt install /tmp/zimg.deb && rm /tmp/zimg.deb

# Install VapourSynth (DEB)
COPY --from=builder /builds/vapoursynth/vapoursynth*.deb /tmp/vapoursynth.deb
RUN apt install /tmp/vapoursynth.deb && rm /tmp/vapoursynth.deb

# Install VapourSynth (WHL)
COPY --from=builder /builds/vapoursynth/bdist_wheel/*.whl /tmp/
RUN pip install --no-index /tmp/*.whl && rm /tmp/*.whl

# Print VapourSynth version
RUN set -ex \
	&& vspipe --version \
	&& python -c 'from vapoursynth import core; print(core.version())'

ENV VS_PLUGINDIR=/usr/lib/vapoursynth
