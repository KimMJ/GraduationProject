FROM ubuntu:16.04

EXPOSE 22

RUN apt-get update && apt-get upgrade -y
RUN apt-get -y install git build-essential cmake pkg-config libjpeg-dev libtiff5-dev 
RUN apt-get -y install libjasper-dev libpng12-dev libavcodec-dev libavformat-dev libswscale-dev libxvidcore-dev 
RUN apt-get -y install libx264-dev libxine2-dev libv4l-dev v4l-utils libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev 
RUN apt-get  -y install libqt4-dev mesa-utils libgl1-mesa-dri libqt4-opengl-dev libatlas-base-dev gfortran libeigen3-dev 
RUN apt-get  -y install wget unzip net-tools vim 

WORKDIR /tl_client
RUN mkdir opencv
WORKDIR /tl_client/opencv
RUN wget -O opencv.zip https://github.com/opencv/opencv/archive/3.4.0.zip
RUN unzip opencv.zip

RUN wget -O opencv_contrib.zip https://github.com/opencv/opencv_contrib/archive/3.4.0.zip
RUN unzip opencv_contrib.zip

WORKDIR /tl_client/opencv/opencv-3.4.0
RUN mkdir build
WORKDIR /tl_client/opencv/opencv-3.4.0/build

RUN cmake -D CMAKE_BUILD_TYPE=RELEASE -D CMAKE_INSTALL_PREFIX=/usr/local -D WITH_TBB=OFF \
                              -D WITH_IPP=OFF -D WITH_1394=OFF -D BUILD_WITH_DEBUG_INFO=OFF \
                              -D BUILD_DOCS=OFF -D INSTALL_C_EXAMPLES=ON -D BUILD_EXAMPLES=OFF \
                              -D BUILD_TESTS=OFF -D BUILD_PERF_TESTS=OFF -D WITH_QT=ON -D WITH_OPENGL=ON \
                              -D OPENCV_EXTRA_MODULES_PATH=../../opencv_contrib-3.4.0/modules \ 
                              -D WITH_V4L=ON -D WITH_FFMPEG=ON -D WITH_XINE=ON -D BUILD_NEW_PYTHON_SUPPORT=ON ..

RUN make -j4
RUN make install

WORKDIR /tl_client
RUN git clone https://github.com/kimmj/GraduationProject.git
WORKDIR /tl_client/GraduationProject
RUN git checkout develop
WORKDIR /tl_client/GraduationProject/epoll/build
RUN cmake ..
RUN make

ENTRYPOINT ["/bin/bash"]
