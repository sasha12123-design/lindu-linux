.PHONY: build docker-build docker-ps clean

build:
	./build/build.sh

docker-build:
	./build/build-in-docker.linux.sh

docker-ps:
	powershell -ExecutionPolicy Bypass -File build/build-in-docker.ps1

clean:
	rm -rf work out