const express = require('express');
const cors = require('cors');
const jwt = require('jsonwebtoken');
const multer = require('multer');
const bodyParser = require('body-parser');
// const { createCanvas, loadImage } = require('canvas');
require('dotenv').config();

const { MongoClient, ServerApiVersion, ObjectId, ISODate } = require('mongodb');
const { error } = require('console');
const app = express();
const port = process.env.PORT || 3005;


const http = require('http').createServer(app);
const io = require('socket.io')(http);
app.use(express.static('public'));
//----------------------avatar api resorce -----------

app.use(bodyParser.json());
app.use(bodyParser.urlencoded({ extended: true }));

// Set up multer for handling file uploads
const storage = multer.memoryStorage();
const upload = multer({ storage: storage });
// -----------------avatar api close ----------

app.use(cors());
app.use(express.json());

const uri = "mongodb+srv://atifsupermart202199:FGzi4j6kRnYTIyP9@cluster0.bfulggv.mongodb.net/?retryWrites=true&w=majority";
const client = new MongoClient(uri, { useNewUrlParser: true, useUnifiedTopology: true, serverApi: ServerApiVersion.v1 });

 
async function run() {
  try {
    await client.connect();
    console.log('db connected');
    const NFCCollection = client.db('NFCDATA').collection('devices');


    app.get('/api/nfcdata', async(req, res) =>{
      const query = {};
      const cursor = NFCCollection.find(query);
      const accounts = await cursor.toArray();
      res.send(accounts);
    })

    app.post('/api/nfc', async (req, res) => {
      const accounts = req.body;
      const result = await NFCCollection.insertOne(accounts);
      res.send("result ddone")
    }); 

    app.delete('/api/data/:id', async (req, res) => {
      const id = req.params.id;
      const query = { _id: ObjectId(id) };
      const result = await NFCCollection.deleteOne(query);
      res.send(result); 

    })

    app.put('api/finalsale', async (req, res) => { 
      const updatedStock = req.body;
      
      // ---------------------------- update close ---------------------------------- 
      res.send({ 'data': 'succesfully data updated',updatedStock  });
    })
 
    
  }
  finally {

  }

}
run().catch(console.dir);




app.get("/", (req, res) => {
  res.send(`<h1 style="text-align: center;
      color: red;"> Server is Running at <span style="color: Blue;">${port}</span></h1>`);
});

app.listen(port, () => {
  console.log("Atif super  mart server running at  : ", port);
});

 