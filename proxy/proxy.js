
const path = require('path');
const net = require('net');
const fs = require('fs');

let verbose = false;
let keepAlive = false;
let eagerSerial = false;

const args = Array.from(process.argv).slice(2).filter(arg => {
    if (arg[0] == '-') {
        switch (arg) {
            case '-v':
            case '--verbose':
                verbose = true;
                break;
            case '-k':
            case '--keepalive':
                keepAlive = true;
                break;
            case '-e':
            case '--eager':
                eagerSerial = true;
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
    node ${relativePath} localhost:2159 8080

arguments:
    -v --verbose  verbose logs
`);
    process.exit(1);
}

const serialDeviceName = args[0];

const isTCP = /[^:]*:[\d+]/.test(serialDeviceName);

const port = +args[1];

const server = new net.Server();

function formatMessage(type, buffer) {
    const header = Buffer.alloc(8);
    header.write('DMA@');
    header.writeInt8(type, 4);
    header.writeIntBE(buffer.length, 5, 3);
    var paddingLength = (16 - ((header.length + buffer.length + 4) & 0xF)) & 0xF;
    return Buffer.concat([header, buffer, Buffer.from('CMPH'), Buffer.alloc(paddingLength)]);
}

function onDataCallback(result) {
    return function(chunk) {
        let currentReadMessage;

        if (currentReadMessage) {
            currentReadMessage = Buffer.concat([currentReadMessage, chunk]);
        } else {
            currentReadMessage = chunk;
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
                    console.log(`Received message of type ${messageType} ${data}`);
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
    }
}

function createSerialPort(devName, onReceiveMessage) {
    return new Promise((resolve, reject) => {
        const writeStream = fs.createWriteStream(devName);
        const readStream = fs.createReadStream(devName);
    
        let writeFd;

        const result = {
            sendMessage: (type, buffer) => {
                const message = formatMessage(type, buffer);
                if (verbose) {
                    console.log(`Sending message to cart: ${type} ${buffer.toString()}`)
                }
                fs.write(writeFd, message, (err) => {
                    if (err) {
                        console.error(err);
                    }
                });
            },
            onReceiveMessage: onReceiveMessage,
            close: () => {
                writeStream.close();
                readStream.close();
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
        });
    
        writeStream.on('close', () => {
            if (verbose) {
                console.log(`Connection to cart closed ${devName}`);
            }
        });

        writeStream.on('error', (err) => {
            console.log(`Error on write stream ${err}`);
        });
    
        readStream.on('data', onDataCallback(result));
    
        readStream.on('error', (err) => {
            console.log(`Error opening read stream ${err}`);
        });
    });
}

function createTCPConnection(address, onReceiveMessage) {
    return new Promise((resolve, reject) => {   
        const parts = address.split(':', 2);
        const socket = net.createConnection(+parts[1], parts[0], () => {
            const result = {
                sendMessage: (type, buffer) => {
                    const message = formatMessage(type, buffer);
                    if (verbose) {
                        console.log(`Sending message to cart: ${type} ${buffer.toString()}`)
                    }
                    socket.write(writeFd, message, (err) => {
                        if (err) {
                            console.error(err);
                        }
                    });
                },
                onReceiveMessage: onReceiveMessage,
                close: () => {
                    socket.destroy();
                },
            };

            socket.on('connect', () => {
                console.log(`Connected to ${address}`);
            });
            
            socket.on('error', (err) => {
                console.log(`Error on socket ${err}`);
            });

            socket.on('data', onDataCallback(result));
        });
    });
}

server.listen(port, function() {
    console.log(`Debugger listening on:${port}`);
});

const MESSAGE_TYPE_TEXT = 1;
const MESSAGE_TYPE_GDB = 4;

let serialPortPromise;
let activeSocket;

function openSerialConnection() {
    if (!serialPortPromise) {
        function onReceiveMessage(message) {
            switch (message.type) {
                case MESSAGE_TYPE_TEXT:
                    console.log(`log: ${message.data.toString('utf8')}`);
                    break;
                case MESSAGE_TYPE_GDB:
                    if (activeSocket) { 
                        activeSocket.write(message.data);
                    }
                    break;
            }
        };

        if (isTCP) {
            serialPortPromise = createTCPConnection(serialDeviceName, onReceiveMessage);
        } else {
            serialPortPromise = createSerialPort(serialDeviceName, onReceiveMessage);
        }
        serialPortPromise.catch(err => console.error(err));
    }
}

if (eagerSerial) {
    openSerialConnection();
}

function findMessageEnd(buffer) {
    let messageEnd = buffer.indexOf('#');
    let interrupt = buffer.indexOf(0x03);

    if (messageEnd != -1 && interrupt != -1) {
        return Math.min(messageEnd + 3, interrupt + 1);
    } else if (messageEnd != -1) {
        return messageEnd + 3;
    } else if (interrupt != -1) {
        return interrupt + 1;
    } else {
        return -1;
    }
}

server.on('connection', function(socket) {
    console.log('Debugger connected');

    let gdbChunk;

    activeSocket = socket;
    
    openSerialConnection();

    socket.on('data', function(chunk) {
        if (gdbChunk) {
            gdbChunk = Buffer.concat([gdbChunk, chunk]);
        } else {
            gdbChunk = chunk;
        }

        serialPortPromise.then(serialPort => {
            // Ensure only complete gdb messages are sent
            let messageEnd = findMessageEnd(gdbChunk);

            while (messageEnd != -1 && messageEnd <= gdbChunk.length) {
                serialPort.sendMessage(MESSAGE_TYPE_GDB, gdbChunk.slice(0, messageEnd));
                gdbChunk = gdbChunk.slice(messageEnd);
                messageEnd = findMessageEnd(gdbChunk);
            }
        });
    });

    socket.on('end', function() {
        console.log('Debugger connection closed');
        serialPortPromise.then(serialPort => serialPort.close());
        serialPortPromise = null;

        if (!keepAlive) {
            server.close();
            process.exit(0);
        }
    });

    socket.on('error', function(err) {
        console.error(`Error: ${err}`);
    });
});