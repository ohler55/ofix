# encoding: UTF-8

class Msg

  attr_reader :name
  attr_reader :type

  def initialize(xe, spec)
    @name = xe.attributes['name']
    @groups = []
    @type = xe.attributes['msgtype']
    @cat = xe.attributes['msgcat']
    @shared = false
    @desc = xe.attributes['description']
    @members = [spec.header]
    xe.nodes.each { |n|
      next unless ('field' == n.name || 'group' == n.name || 'component' == n.name)
      @members << Member.new(n)
    }
    @members << spec.trailer
  end

  def tid()
    id = 0
    @type.bytes.each { |b|
      id = (id << 8) + b
    }
    id
  end

  def gen_c(f, spec)
    f.write("// #{@name} [#{@type}]\n\n")
    # TBD groups

    tagSeq = [0] * 1000
    seq = 1
    @members.each { |m|
      # TBD check kind
      unless (mf = spec.find_field(m.name)).nil?
        pos = mf.tag
        tagSeq[pos] = seq if 0 < pos && pos < 1000
      end

      #raise Exception.new("Failed to find field #{m.name}") if (mf = spec.find_field(m.name)).nil?
      seq += 1
    }
    f.write(%|
static struct _ofixMsgSpec	#{@name} = {
    &fix#{spec.major}#{spec.minor}Spec, // version
    #{tid()}, // tid
    "#{@type}", // type
    "#{@name}", // name
    {#{tagSeq.join(',')}},
    
|)

  end

end # Msg
