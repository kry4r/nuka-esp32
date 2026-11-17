var s=async t=>{let o;import("./install-dialog-im156JnI-SUF2TKX2.js").then((function(e){return e.i}));try{o=await navigator.serial.requestPort()}catch(e){return e.name==="NotFoundError"?void import("./index-Dt9w-IG1-GB7APVGA.js").then((i=>i.openNoPortPickedDialog((()=>s(t))))):void alert(`Error: ${e.message}`)}if(!o)return;try{await o.open({baudRate:115200,bufferSize:8192})}catch(e){return void alert(e.message)}let n=document.createElement("ewt-install-dialog");n.port=o,n.manifestPath=t.manifest||t.getAttribute("manifest"),n.overrides=t.overrides,n.addEventListener("closed",(()=>{o.close()}),{once:!0}),document.body.appendChild(n)},r=class t extends HTMLElement{connectedCallback(){if(this.renderRoot)return;if(this.renderRoot=this.attachShadow({mode:"open"}),!t.isSupported||!t.isAllowed)return this.toggleAttribute("install-unsupported",!0),void(this.renderRoot.innerHTML=t.isAllowed?"<slot name='unsupported'>Your browser does not support installing things on ESP devices. Use Mozilla Firefox, Google Chrome or Microsoft Edge.</slot>":"<slot name='not-allowed'>You can only install ESP devices on HTTPS websites or on the localhost.</slot>");this.toggleAttribute("install-supported",!0);let o=document.createElement("slot");o.addEventListener("click",(async e=>{e.preventDefault(),s(this)})),o.name="activate";let n=document.createElement("button");if(n.innerText="Connect",o.append(n),"adoptedStyleSheets"in Document.prototype&&"replaceSync"in CSSStyleSheet.prototype){let e=new CSSStyleSheet;e.replaceSync(t.style),this.renderRoot.adoptedStyleSheets=[e]}else{let e=document.createElement("style");e.innerText=t.style,this.renderRoot.append(e)}this.renderRoot.append(o)}};r.isSupported="serial"in navigator,r.isAllowed=window.isSecureContext,r.style=`
  button {
    position: relative;
    cursor: pointer;
    font-size: 14px;
    font-weight: 500;
    padding: 10px 24px;
    color: var(--esp-tools-button-text-color, #fff);
    background-color: var(--esp-tools-button-color, #03a9f4);
    border: none;
    border-radius: var(--esp-tools-button-border-radius, 9999px);
  }
  button::before {
    content: " ";
    position: absolute;
    top: 0;
    bottom: 0;
    left: 0;
    right: 0;
    opacity: 0.2;
    border-radius: var(--esp-tools-button-border-radius, 9999px);
  }
  button:hover::before {
    background-color: rgba(255,255,255,.8);
  }
  button:focus {
    outline: none;
  }
  button:focus::before {
    background-color: white;
  }
  button:active::before {
    background-color: grey;
  }
  :host([active]) button {
    color: rgba(0, 0, 0, 0.38);
    background-color: rgba(0, 0, 0, 0.12);
    box-shadow: none;
    cursor: unset;
    pointer-events: none;
  }
  .hidden {
    display: none;
  }`,customElements.define("esp-web-install-button",r);
