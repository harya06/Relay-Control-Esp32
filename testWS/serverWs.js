const WebSocket = require('ws');

const PORT = 8080;
const PATH = '/ws'; 

const wss = new WebSocket.Server({ port: PORT, path: PATH });

console.log(`[SERVER] WebSocket server dimulai di: ws://localhost:${PORT}${PATH}`);


wss.on('connection', (ws, req) => {

    const clientIp = req.socket.remoteAddress;
    
    console.log(`[KONEKSI] Klien baru terhubung. IP: ${clientIp}`);

    ws.on('message', (message) => {
        const messageString = message.toString();
        
        console.log(`[PESAN DARI ${clientIp}] ${messageString}`);
        
        wss.clients.forEach((client) => {
            if (client !== ws && client.readyState === WebSocket.OPEN) {
                client.send(messageString);
            }
        });
    });

    // Saat Klien terputus...
    ws.on('close', () => {
        console.log(`[KONEKSI] Klien ${clientIp} terputus.`);
    });

    // Menangani error
    ws.on('error', (error) => {
        console.error(`[ERROR] dari ${clientIp}:`, error.message);
    });
});