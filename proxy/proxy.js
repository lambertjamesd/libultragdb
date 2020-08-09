
const path = require('path');
const net = require('net');
const fs = require('fs');

if (process.argv.length != 4) {
    const relativePath = path.relative(process.cwd(), process.argv[1]);

    process.stdout.write(
`usage:
    node ${relativePath} <cart serial port> <port>
example
    node ${relativePath} /dev/ttyUSB0 8080
`);
    process.exit(1);
}

const serialDeviceName = process.argv[2];
const port = +process.argv[3];

const server = new net.Server();

server.listen(port, function() {
    console.log(`Debugger listening on:${port}`);
});

server.on('connection', function(socket) {
    console.log('Debugger connected');
    // socket.write('Hello, client.');
    
    const writeStream = fs.createWriteStream(serialDeviceName);

    let writeFd;

    writeStream.on('open', (fd) => {
        writeFd = fd;
        console.log(`Connected to flash cart ${serialDeviceName}`);
    });

    writeStream.on('ready', () => {
        setInterval(() => {
            if (typeof writeFd === 'number') {
                fs.write(writeFd, Buffer.from('Hello World!', 'utf8'), (err, bytesWritten) => {
                    console.log(`Bytes written ${err}, ${bytesWritten}`);
    
                });
            }
        }, 1000);
    });

    writeStream.on('close', () => {
        writeFd = undefined;
    });


    const readStream = fs.createReadStream(serialDeviceName);

    readStream.on('data', (chunk) => {
        console.log(`Received from cart ${chunk.toString()}`);
    });

    readStream.on('error', (err) => {
        console.log(`Error opening read strem ${err}`);
    });

    socket.on('data', function(chunk) {
        console.log(`Data received from client: ${chunk.toString()}`);
    });

    socket.on('end', function() {
        console.log('Debugger connection closed');
        process.exit(0);
    });

    socket.on('error', function(err) {
        console.log(`Error: ${err}`);
    });
});