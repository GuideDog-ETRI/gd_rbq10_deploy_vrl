// Execute actual generated HTML drawing functions with a recording canvas (no GUI).
const fs=require('fs'),vm=require('vm'),assert=require('assert');
const html=fs.readFileSync(process.argv[2],'utf8'),calls={camera:[],grid:[]},nodes={};
for(const id of ['camera','grid'])nodes[id]={getContext(){return {clearRect(){},fillRect(...x){calls[id].push(x)}}}};
nodes.step={value:'0'};nodes.label={};
const ctx={document:{getElementById(id){return nodes[id]}}};
vm.createContext(ctx);
vm.runInContext(html.match(/<script>([\s\S]*?)<\/script>/)[1],ctx);
assert.equal(calls.grid.length,187*6);
assert.deepEqual(calls.grid[0],[200,240,19,14]); // (row0,col0): bottom right
assert.deepEqual(calls.grid[186],[0,0,19,14]); // (row10,col16): top left
assert.deepEqual(calls.grid[5*17+8],[100,120,19,14]);
assert.deepEqual(calls.camera[0],[88,0,2,2]); // CW90
assert.deepEqual(calls.camera[44*80+79],[0,158,2,2]);
console.log('actual offline HTML grid/CW90 rendering: PASS');
