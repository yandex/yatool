FROM ubuntu:22.04

RUN apt-get update && apt-get install -y python3 clang-14 openjdk-17-jdk

CMD [ "/source_root/devtools/ya/bootstrap/stage1.sh", "/source_root", "/build_root", "/result" ]
