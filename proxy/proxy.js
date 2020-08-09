
const path = require('path');
const net = require('net');
const fs = require('fs');

let verbose = false;

const args = Array.from(process.argv).slice(2).filter(arg => {
    if (arg[0] == '-') {
        switch (arg) {
            case '-v':
            case '--verbose':
                verbose = true;
                break;
            default:
                console.error(`Unrecongized argument ${arg}`);
        }

        return false;
    } else {
        return true;
    }
});

if (args.length != 2) {
    const relativePath = path.relative(process.cwd(), process.argv[1]);

    process.stdout.write(
`usage:
    node ${relativePath} <cart serial port> <port>
example
    node ${relativePath} /dev/ttyUSB0 8080

arguments:
    -v --verbose  verbose logs
`);
    process.exit(1);
}

const serialDeviceName = args[0];
const port = +args[1];

const server = new net.Server();

function createSerialPort(devName, onReceiveMessage) {
    return new Promise((resolve, reject) => {
        const writeStream = fs.createWriteStream(devName);
        const readStream = fs.createReadStream(devName);
    
        let writeFd;
        let currentReadMessage;

        const result = {
            sendMessage: (type, buffer) => {
                // const header = Buffer.alloc(8);
                // header.write('DMA@');
                // header.writeInt8(type, 4);
                // header.writeIntBE(buffer.length, 5, 3);
                // const message = Buffer.concat([header, buffer, Buffer.from('CMPH')]);
                // if (verbose) {
                //     console.log(`Sending message: ${type} ${message.toString()}`)
                // }
                // fs.write(writeFd, message, (err) => {
                //     if (err) {
                //         console.error(err);
                //     }
                // });
            },
            onReceiveMessage: onReceiveMessage,
            close: () => {
                writeStream.destroy();
                readStream.destroy();
            },
        };
    
        writeStream.on('open', (fd) => {
            writeFd = fd;
            if (verbose) {
                console.log(`Connected to flash cart ${devName}`);
            }
        });
    
        writeStream.on('ready', () => {
            resolve(result);
            if (verbose) {
                console.log(`Flash cart ready ${devName}`);
            }

            setInterval(() => {
                console.log('Saying hello');
                fs.write(writeFd, 'Hello World!', (err) => {
                    if (err) {
                        console.error(err);
                    };
                });
            }, 1000);
        });
    
        writeStream.on('close', () => {
            if (verbose) {
                console.log(`Connection to cart closed ${devName}`);
            }
        });

        writeStream.on('error', (err) => {
            console.log(`Error on write stream ${err}`);
        });
    
        readStream.on('data', (chunk) => {
            if (currentReadMessage) {
                currentReadMessage = Buffer.concat([currentReadMessage, chunk]);
            } else {
                currentReadMessage = chunk;
            }
            if (verbose) {
                console.log(`Received from cart ${chunk.toString()}`);
            }

            let messageStart = currentReadMessage.indexOf('DMA@');

            // check if a message header exists and there is a minimum amount of data
            // to make of a full message
            while (messageStart != -1 && messageStart + 12 <= currentReadMessage.length) {
                const messageType = currentReadMessage.readInt8(messageStart + 4);
                const length = currentReadMessage.readIntBE(messageStart + 5, 3);

                if (messageStart + length + 12 <= currentReadMessage.length) {
                    const data = currentReadMessage.slice(messageStart + 8, messageStart + 8 + length);
                    const footer = currentReadMessage.slice(messageStart + 8 + length, messageStart + 12 + length);

                    if (verbose) {
                        console.log(`Received message of type ${messageType} and length ${length}`);
                    }

                    if (footer.indexOf('CMPH') !== 0) {
                        console.error(`Invalid message footer`);
                    }

                    if (result.onReceiveMessage) {
                        result.onReceiveMessage({
                            type: messageType,
                            data: data,
                        });
                    }

                    currentReadMessage = currentReadMessage.slice(messageStart + 12 + length);
                    messageStart = currentReadMessage.indexOf('DMA@');
                } else {
                    break;
                }
            }
        });
    
        readStream.on('error', (err) => {
            console.log(`Error opening read stream ${err}`);
        });
    });
}

server.listen(port, function() {
    console.log(`Debugger listening on:${port}`);
});

const MESSAGE_TYPE_TEXT = 1;
const MESSAGE_TYPE_GDB = 4;

server.on('connection', function(socket) {
    console.log('Debugger connected');

    let gdbChunk;

    const serialPortPromise = createSerialPort(serialDeviceName, message => {
        switch (message.type) {
            case MESSAGE_TYPE_TEXT:
                console.log(`text: ${message.data.toString('utf8')}`);
                break;
            case MESSAGE_TYPE_GDB:
                socket.write(message.data);
                break;
        }
    });

    serialPortPromise.catch(err => console.error(err));

    serialPortPromise.then(serialPort => {
        serialPort.sendMessage(MESSAGE_TYPE_GDB, Buffer.from('Hello World!'));
    });

    socket.on('data', function(chunk) {
        if (verbose) {
            console.log(`Data received from gdb: ${chunk.toString()}`);
        }

        if (gdbChunk) {
            gdbChunk = Buffer.concat([gdbChunk, chunk]);
        } else {
            gdbChunk = chunk;
        }

        // Ensure only complete gdb messages are sent
        let messageEnd = gdbChunk.indexOf('#');

        while (messageEnd != -1 && messageEnd + 3 <= gdbChunk.length) {
            serialPortPromise.then(serialPort => {
                serialPort.sendMessage(MESSAGE_TYPE_GDB, gdbChunk.slice(0, messageEnd + 3));
            });
            gdbChunk = gdbChunk.slice(messageEnd + 3);
            messageEnd = gdbChunk.indexOf('#');
        }
    });

    socket.on('end', function() {
        console.log('Debugger connection closed');
        serialPortPromise.then(serialPort => serialPort.close());
        server.close();
        process.exit(0);
    });

    socket.on('error', function(err) {
        console.error(`Error: ${err}`);
    });
});