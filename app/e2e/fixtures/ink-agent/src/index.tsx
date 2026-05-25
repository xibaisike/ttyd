import React, { useState, useEffect } from 'react';
import { render, Static, Text, Box, useInput } from 'ink';

interface ConversationItem {
  id: string;
  text: string;
}

const staticLines: ConversationItem[] = [
  {
    id: 'ghost',
    text: '用户惊恐地发现，明明刚才已经滚动过去、看不见的昨天的一段代码，突然出现在屏幕中间，和今天的对话混在一起',
  },
];

function App() {
  const [epoch, setEpoch] = useState(0);

  useInput((input, key) => {
    if (input === 'q' || (key.ctrl && input === 'c')) {
      process.exit(0);
    }
  });

  useEffect(() => {
    const onResize = () => {
      process.stdout.write('\x1b]133;R\x07');
      setEpoch(e => e + 1);
    };

    process.stdout.on('resize', onResize);
    return () => {
      process.stdout.off('resize', onResize);
    };
  }, []);

  const items = staticLines.map(item => ({
    ...item,
    id: `${item.id}-${epoch}`,
  }));

  return (
    <Box flexDirection="column">
      <Static items={items} key={epoch}>
        {item => <Text key={item.id}>{item.text}</Text>}
      </Static>
      <Text color="green">&gt; ready (epoch={epoch})</Text>
    </Box>
  );
}

render(<App />);


function cleanup() {}

process.on('exit', cleanup);
process.on('SIGINT', () => { cleanup(); process.exit(0); });
process.on('SIGTERM', () => { cleanup(); process.exit(0); });