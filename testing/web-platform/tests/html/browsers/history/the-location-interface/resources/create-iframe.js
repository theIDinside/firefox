async function configAndNavigateIFrame(iframe, { src, name, referrerPolicy, sandbox}) {
  iframe.name = name;
  iframe.style.width = "100%";
  iframe.style.height = "100%";
  iframe.referrerPolicy = referrerPolicy
  if (sandbox) {
    // postMessage needs to work
    iframe.setAttribute("sandbox", "allow-scripts");
  }
  document.body.appendChild(iframe);
  await new Promise((resolve) => {
    iframe.addEventListener("load", resolve, { once: true });
    iframe.src = src;
  });
}