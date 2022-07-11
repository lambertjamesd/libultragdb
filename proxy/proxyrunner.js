
const fs = require('fs');
const child_process = require('child_process');

const prevProcesses = 'prev_process_ids';

if (fs.existsSync('prev_process_ids')) {
    const all = fs.readFileSync(prevProcesses, 'utf8');
    all.split(',').forEach(processId => {
        const pid = +processId;

        if (pid && !isNaN(pid)) {
            try {
                process.kill(pid);
            } catch (_) {
                
            }
        }
    });
}


const port = process.argv[1];

const proxy = child_process.spawn(
    'node', 
    ['/home/james/libultragdb/proxy/proxy.js', '/dev/ttyUSB0', String(port), '-v'],
    {
        detached: true,
    }
);

fs.writeFileSync(prevProcesses, String(proxy.pid));
